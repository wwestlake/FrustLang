// Implementation - see the header for the full design writeup. Mirrors
// frust_compiler's Main.cpp JIT path (RunViaJit/ParseSource) closely,
// generalized to support multiple independently loadable/unloadable
// plugins instead of one throwaway run.

#include "frust_plugin_host/FrustPluginHost.h"
#include "frust_plugin_host/FrustPluginManifest.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "AST.h"
#include "ASTHash.h"
#include "Codegen.h"
#include "Lexer.h"
#include "ModuleLoader.h"
#include "parser.hpp"

#include <llvm/ExecutionEngine/Orc/Core.h>
#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/Support/TargetSelect.h>

using namespace frust;

struct FrustPluginHandleImpl; // full definition below; only a pointer is needed as a map key here

namespace {

// Everything here is process-wide by design (see header: the host-
// function registry and the underlying LLJIT are shared across every
// plugin load - only each plugin's own JITDylib is isolated).
struct HostState {
    std::mutex mutex;
    std::unique_ptr<llvm::orc::LLJIT> jit;
    llvm::orc::JITDylib* hostDylib = nullptr;
    uint64_t nextPluginId = 0;
    bool initAttempted = false;

    // This host's own declared identity - see
    // frust_plugin_host_set_application_identity's header doc. Process-
    // wide like everything else here, not per-thread - a host only has
    // one identity regardless of which thread asks.
    std::string applicationId;

    // AST content hash (FRUST_LANG_SPEC.md 1.4) of the last successfully
    // loaded program for each path - lets frust_plugin_reload skip the
    // full unload/recompile/relink cycle when a reload is triggered but
    // nothing structurally changed (a no-op save, a file watcher firing
    // twice). Called with `mutex` already held, same as everything else
    // here.
    std::map<std::string, std::string> lastAstHashByPath;

    // Event subscription registry (docs/17-Plugin-Automation-Layer.md).
    // eventHandlers is the fast path fire() actually iterates.
    // handlersByPlugin exists purely so unload() can find and purge
    // exactly the handlers a given plugin owns - without it, a stale
    // handler pointing into that plugin's now-freed JIT code would be a
    // use-after-free the next time its event fired. Both guarded by
    // `mutex`, same as everything else here.
    std::unordered_map<std::string, std::vector<void(*)(void*)>> eventHandlers;
    std::unordered_map<FrustPluginHandleImpl*, std::vector<std::pair<std::string, void(*)(void*)>>> handlersByPlugin;

    // Plugin-to-plugin service registry (docs/17 section 4.3) - name ->
    // whatever opaque pointer was registered under it. servicesByPlugin
    // mirrors handlersByPlugin's role: lets unload() find and purge
    // exactly the service names a given plugin owns.
    std::unordered_map<std::string, void*> services;
    // (name, pointer) pairs, not just names - a later registration under
    // the same name from a DIFFERENT plugin can overwrite an earlier
    // one (see frust_register_service's "last call wins" convention),
    // so unload() must only erase services[name] if it still holds THIS
    // plugin's own pointer, not unconditionally by name.
    std::unordered_map<FrustPluginHandleImpl*, std::vector<std::pair<std::string, void*>>> servicesByPlugin;

    // Called with `mutex` already held.
    bool ensureInit() {
        if (jit) return true;
        if (initAttempted) return false; // already tried and failed once
        initAttempted = true;

        llvm::InitializeNativeTarget();
        llvm::InitializeNativeTargetAsmPrinter();
        llvm::InitializeNativeTargetAsmParser();

        auto jitOrErr = llvm::orc::LLJITBuilder().create();
        if (!jitOrErr) {
            std::cerr << "frust_plugin_host: JIT init failed: " << llvm::toString(jitOrErr.takeError()) << "\n";
            return false;
        }
        jit = std::move(*jitOrErr);

        // A dedicated JITDylib for host-registered functions (see
        // frust_plugin_register_host_function) plus process-exported
        // symbols (frust_print_str-style dllexports) - every plugin
        // JITDylib links against this one so `extern fn` resolves both
        // ways described in the header, without re-adding either
        // generator/registry per plugin load.
        auto hostJDOrErr = jit->createJITDylib("frust_plugin_host.hostfns");
        if (!hostJDOrErr) {
            std::cerr << "frust_plugin_host: failed to create host function dylib\n";
            jit.reset();
            return false;
        }
        hostDylib = &*hostJDOrErr;

        auto generator = llvm::orc::DynamicLibrarySearchGenerator::GetForCurrentProcess(
            jit->getDataLayout().getGlobalPrefix());
        if (generator) {
            hostDylib->addGenerator(std::move(*generator));
        }

        return true;
    }
};

HostState& state() {
    static HostState s;
    return s;
}

// Set only around frust_plugin_call_on_init()'s call into a plugin's
// own `on_init` - the window during which frust_register_event_handler
// can attribute a registration to a specific plugin for automatic
// cleanup on unload. thread_local so concurrent init calls on different
// threads (unlikely today, but not precluded) don't cross-contaminate
// ownership.
thread_local FrustPluginHandleImpl* g_registeringHandle = nullptr;

// See frust_plugin_last_error()'s declaration - mirrors every existing
// std::cerr write (unchanged, still happens) into a thread-local string
// a GUI host with no visible console can actually show the user.
thread_local std::string g_lastError;

void reportError(const std::string& msg) {
    std::cerr << "frust_plugin_host: " << msg << "\n";
    g_lastError = msg;
}

// Parses a single .frust file - same shape as Main.cpp's ParseSource,
// duplicated here rather than shared because it isn't part of
// frust_lang's public header surface (it's a free function local to
// frust_compiler's own Main.cpp).
Program* ParsePluginSource(std::istream& input, AstArena& arena, std::vector<std::string>& parseErrors) {
    Lexer lexer(&input);
    Program* result = nullptr;
    std::vector<ParseError> structuredErrors; // unused here, same as Main.cpp's own ParseSource
    Parser parser(lexer, arena, parseErrors, result, structuredErrors);
    parser.parse();
    parseErrors.insert(parseErrors.end(), lexer.errors.begin(), lexer.errors.end());
    if (result) ResolveImports(result, arena, parseErrors);
    return result;
}

// Shared Phase 1 of frust_plugin_load()/frust_plugin_peek_manifest():
// parse `path` and codegen it into a fresh, not-yet-linked
// llvm::Module - reports the error (reportError) and returns false on
// any failure, in which case the caller has nothing further to do.
// Takes s.mutex internally (ensureInit/computeAstHash's own needs) but
// releases it before returning either way - callers do their own
// manifest-reading work outside any lock, same reasoning as before
// (IsCompatible()'s dependents take that same lock themselves).
bool CompilePluginModule(const std::string& path,
                          std::unique_ptr<llvm::LLVMContext>& outContext,
                          std::unique_ptr<llvm::Module>& outModule,
                          std::string& outAstHash) {
    auto& s = state();
    std::lock_guard<std::mutex> lock(s.mutex);
    if (!s.ensureInit()) return false;

    std::ifstream file(path);
    if (!file) {
        reportError("cannot open '" + path + "'");
        return false;
    }

    AstArena arena;
    std::vector<std::string> parseErrors;
    Program* prog = ParsePluginSource(file, arena, parseErrors);
    if (!parseErrors.empty() || !prog) {
        std::string msg = std::to_string(parseErrors.size()) + " error(s) loading '" + path + "'";
        for (const auto& err : parseErrors) msg += "\n  " + err;
        reportError(msg);
        return false;
    }

    outAstHash = computeAstHash(*prog);
    outContext = std::make_unique<llvm::LLVMContext>();
    outModule = std::make_unique<llvm::Module>(path, *outContext);

    Codegen codegen(*outContext, *outModule);
    if (!codegen.compileProgram(*prog)) {
        reportError("codegen failed for '" + path + "'");
        return false;
    }
    return true;
}

// Shared Phase 2 (part A): finds the embedded manifest global on an
// already-compiled module (Codegen::compileManifestDecl's
// kFrustPluginManifestGlobalName), or nullptr if the source had no
// `manifest "...";` declaration at all. AllowInternal=true is
// required: the global is PrivateLinkage (deliberately, so it's never
// a resolvable extern JIT symbol - see compileManifestDecl's comment),
// and Module::getGlobalVariable() silently returns null for local-
// linkage globals unless explicitly told to allow them.
llvm::GlobalVariable* FindManifestGlobal(llvm::Module& module) {
    auto* gv = module.getGlobalVariable(kFrustPluginManifestGlobalName, /*AllowInternal=*/true);
    return (gv && gv->hasInitializer()) ? gv : nullptr;
}

// Shared Phase 2 (part B): pulls the raw JSON text out of a manifest
// global already confirmed to exist by FindManifestGlobal. nullopt
// only for a genuinely malformed constant (wrong LLVM constant kind) -
// should not happen for anything Codegen itself ever emitted, but
// checked rather than assumed.
std::optional<std::string> ExtractManifestString(llvm::GlobalVariable& manifestGV) {
    auto* manifestData = llvm::dyn_cast<llvm::ConstantDataArray>(manifestGV.getInitializer());
    if (!manifestData || !manifestData->isCString()) return std::nullopt;
    return manifestData->getAsCString().str();
}

} // namespace

struct FrustPluginHandleImpl {
    std::string path;
    std::string dylibName;
    llvm::orc::JITDylib* dylib = nullptr;
    // Always populated for a handle actually returned by
    // frust_plugin_load() - "no manifest, no load" means a plugin that
    // got this far necessarily had one, already parsed and verified
    // compatible. See frust_plugin_get_manifest.
    frust_plugin_host::PluginManifest manifest;
};

extern "C" {

FRUST_PLUGIN_HOST_API FrustPluginHandle frust_plugin_load(const char* path) {
    auto& s = state();

    // Phase 1: parse + codegen, into a fresh, not-yet-linked module.
    std::unique_ptr<llvm::LLVMContext> context;
    std::unique_ptr<llvm::Module> module;
    std::string astHash;
    if (!CompilePluginModule(path, context, module, astHash)) return nullptr;

    // Phase 2: NO MANIFEST, NO LOAD. Read the embedded manifest straight
    // off the freshly-compiled module - before this module is ever
    // linked into the JIT or a single instruction of it runs.
    // Deliberately OUTSIDE s.mutex: frust_plugin_host::IsCompatible()
    // calls both frust_plugin_host_application_identity() and
    // frust_plugin_is_host_function_available(), which each take that
    // same lock themselves.
    auto* manifestGV = FindManifestGlobal(*module);
    if (!manifestGV) {
        reportError("refusing to load '" + std::string(path) + "': no embedded plugin manifest "
            "(every plugin must declare its own 'manifest \"{ ... }\";' - see docs/17-Plugin-Automation-Layer.md)");
        return nullptr;
    }
    auto manifestJson = ExtractManifestString(*manifestGV);
    if (!manifestJson) {
        reportError("refusing to load '" + std::string(path) + "': malformed embedded manifest constant");
        return nullptr;
    }

    auto parsedManifest = frust_plugin_host::ParseManifestJson(*manifestJson, path);
    if (!parsedManifest) {
        reportError("refusing to load '" + std::string(path) + "': embedded manifest failed to parse "
            "(see stderr above for the JSON error)");
        return nullptr;
    }
    if (!frust_plugin_host::IsCompatible(*parsedManifest)) {
        reportError("refusing to load '" + std::string(path) + "': " +
            frust_plugin_host::LastIncompatibilityReason());
        return nullptr;
    }

    // Phase 3: compatible - actually link it into the JIT. s.ensureInit()
    // already succeeded in Phase 1 and s.jit persists across the gap.
    std::lock_guard<std::mutex> lock(s.mutex);

    std::string dylibName = "frust_plugin#" + std::to_string(s.nextPluginId++) + ":" + path;
    auto jdOrErr = s.jit->createJITDylib(dylibName);
    if (!jdOrErr) {
        reportError("failed to create JITDylib for '" + std::string(path) + "'");
        return nullptr;
    }
    llvm::orc::JITDylib& JD = *jdOrErr;
    JD.addToLinkOrder(*s.hostDylib);

    llvm::orc::ThreadSafeContext tsc(std::move(context));
    llvm::orc::ThreadSafeModule tsm(std::move(module), tsc);
    if (auto err = s.jit->addIRModule(JD, std::move(tsm))) {
        reportError("JIT module load failed for '" + std::string(path) + "': " + llvm::toString(std::move(err)));
        if (auto rmErr = s.jit->getExecutionSession().removeJITDylib(JD)) {
            llvm::consumeError(std::move(rmErr));
        }
        return nullptr;
    }

    s.lastAstHashByPath[path] = astHash;

    auto* handle = new FrustPluginHandleImpl();
    handle->path = path;
    handle->dylibName = dylibName;
    handle->dylib = &JD;
    handle->manifest = std::move(*parsedManifest);
    return handle;
}

FRUST_PLUGIN_HOST_API FrustPluginManifestHandle frust_plugin_get_manifest(FrustPluginHandle handle) {
    if (!handle) return nullptr;
    return frust_plugin_host::WrapManifest(handle->manifest);
}

FRUST_PLUGIN_HOST_API FrustPluginManifestHandle frust_plugin_peek_manifest(const char* path) {
    // Same Phase 1/2 as frust_plugin_load (parse+codegen, then read the
    // embedded manifest global) - but stops there. No compatibility
    // check, no JIT link, no "loading" of any kind - context/module are
    // simply discarded (RAII) once the manifest string is extracted.
    // For a plugin BROWSER that wants to show every discovered plugin's
    // metadata, including an incompatible one (so the user can see
    // why), without committing to loading any of them.
    std::unique_ptr<llvm::LLVMContext> context;
    std::unique_ptr<llvm::Module> module;
    std::string astHash;
    if (!CompilePluginModule(path, context, module, astHash)) return nullptr;

    auto* manifestGV = FindManifestGlobal(*module);
    if (!manifestGV) {
        reportError("no embedded plugin manifest in '" + std::string(path) + "'");
        return nullptr;
    }
    auto manifestJson = ExtractManifestString(*manifestGV);
    if (!manifestJson) {
        reportError("malformed embedded manifest constant in '" + std::string(path) + "'");
        return nullptr;
    }

    auto parsedManifest = frust_plugin_host::ParseManifestJson(*manifestJson, path);
    if (!parsedManifest) {
        reportError("embedded manifest in '" + std::string(path) + "' failed to parse "
            "(see stderr above for the JSON error)");
        return nullptr;
    }
    return frust_plugin_host::WrapManifest(std::move(*parsedManifest));
}

FRUST_PLUGIN_HOST_API void frust_plugin_unload(FrustPluginHandle handle) {
    if (!handle) return;
    auto& s = state();
    std::lock_guard<std::mutex> lock(s.mutex);

    // Purge any event handlers this plugin registered (during its own
    // on_init, via frust_register_event_handler) before its code goes
    // away - a handler left behind would point into freed JIT code, a
    // real use-after-free the next time that event fired.
    auto ownedIt = s.handlersByPlugin.find(handle);
    if (ownedIt != s.handlersByPlugin.end()) {
        for (auto& owned : ownedIt->second) {
            auto& vec = s.eventHandlers[owned.first];
            vec.erase(std::remove(vec.begin(), vec.end(), owned.second), vec.end());
        }
        s.handlersByPlugin.erase(ownedIt);
    }

    // Same purge for services this plugin registered - a stale service
    // pointer into unloaded JIT code would be a use-after-free for
    // whoever looks it up next.
    auto ownedServicesIt = s.servicesByPlugin.find(handle);
    if (ownedServicesIt != s.servicesByPlugin.end()) {
        for (auto& owned : ownedServicesIt->second) {
            auto svcIt = s.services.find(owned.first);
            // Only erase if the registry still points at what THIS
            // plugin registered - a later plugin may have overwritten
            // it under the same name, and that registration must survive.
            if (svcIt != s.services.end() && svcIt->second == owned.second) {
                s.services.erase(svcIt);
            }
        }
        s.servicesByPlugin.erase(ownedServicesIt);
    }

    if (s.jit && handle->dylib) {
        if (auto err = s.jit->getExecutionSession().removeJITDylib(*handle->dylib)) {
            reportError("unload of '" + handle->path + "' failed: " + llvm::toString(std::move(err)));
        }
    }
    delete handle;
}

FRUST_PLUGIN_HOST_API FrustPluginHandle frust_plugin_reload(FrustPluginHandle handle) {
    if (!handle) return nullptr;
    std::string path = handle->path; // copy before unload deletes the handle

    // AST content hash check (FRUST_LANG_SPEC.md 1.4): was an unconditional
    // unload+load on every call - the full LLVM module recompile + JITDylib
    // teardown/relink cost was paid even when the source hadn't
    // structurally changed since the last successful load (a no-op save,
    // an editor/file-watcher firing the reload twice for one real edit).
    // Parsing here is cheap relative to that; if the freshly-parsed AST
    // hashes the same as what's already loaded, skip the real work
    // entirely and hand back the existing handle untouched.
    {
        std::ifstream file(path);
        if (file) {
            AstArena arena;
            std::vector<std::string> parseErrors;
            Program* prog = ParsePluginSource(file, arena, parseErrors);
            if (prog && parseErrors.empty()) {
                std::string newHash = computeAstHash(*prog);
                auto& s = state();
                std::lock_guard<std::mutex> lock(s.mutex);
                auto it = s.lastAstHashByPath.find(path);
                if (it != s.lastAstHashByPath.end() && it->second == newHash) {
                    return handle; // unchanged - nothing to reload
                }
            }
            // Parse failed or errored: fall through to the real reload path
            // below, which will report the error the normal way.
        }
    }

    frust_plugin_unload(handle);
    FrustPluginHandle newHandle = frust_plugin_load(path.c_str());
    // Was missing entirely - a real (content-changed) reload correctly
    // purged the OLD handle's event/service registrations (unload()
    // above) but never re-created them on the new one, so anything a
    // plugin registered during on_init silently vanished on every real
    // edit-and-reload. frust_plugin_load() itself deliberately does NOT
    // call on_init (a fresh load and a reload have always been distinct
    // operations - a host might want to defer initialization), so this
    // has to happen here, specifically on the reload path.
    if (newHandle) frust_plugin_call_on_init(newHandle);
    return newHandle;
}

FRUST_PLUGIN_HOST_API void* frust_plugin_get_fn(FrustPluginHandle handle, const char* name) {
    if (!handle) return nullptr;
    auto& s = state();
    std::lock_guard<std::mutex> lock(s.mutex);
    if (!s.jit || !handle->dylib) return nullptr;

    auto sym = s.jit->lookup(*handle->dylib, name);
    if (!sym) {
        llvm::consumeError(sym.takeError());
        return nullptr;
    }
    return sym->toPtr<void*>();
}

FRUST_PLUGIN_HOST_API void frust_plugin_register_host_function(const char* name, void* fn_ptr) {
    auto& s = state();
    std::lock_guard<std::mutex> lock(s.mutex);
    if (!s.ensureInit()) return;

    llvm::orc::MangleAndInterner mangle(s.jit->getExecutionSession(), s.jit->getDataLayout());
    auto err = s.hostDylib->define(llvm::orc::absoluteSymbols({
        { mangle(name), llvm::orc::ExecutorSymbolDef(llvm::orc::ExecutorAddr::fromPtr(fn_ptr), llvm::JITSymbolFlags::Exported) }
    }));
    if (err) {
        reportError("failed to register host function '" + std::string(name) + "': " + llvm::toString(std::move(err)));
    }
}

FRUST_PLUGIN_HOST_API const char* frust_plugin_last_error(void) {
    return g_lastError.c_str();
}

FRUST_PLUGIN_HOST_API int32_t frust_plugin_is_host_function_available(const char* name) {
    if (!name) return 0;
    auto& s = state();
    std::lock_guard<std::mutex> lock(s.mutex);
    if (!s.ensureInit() || !s.hostDylib) return 0;

    // A real symbol lookup, not just "was register_host_function() ever
    // called with this name" - also true for anything the host process
    // itself exports (the DynamicLibrarySearchGenerator path
    // ensureInit() already wired into hostDylib), matching exactly what
    // an `extern fn` in a plugin would actually resolve against.
    auto sym = s.jit->lookup(*s.hostDylib, name);
    if (!sym) {
        llvm::consumeError(sym.takeError());
        return 0;
    }
    return 1;
}

FRUST_PLUGIN_HOST_API void frust_plugin_host_set_application_identity(const char* appId) {
    auto& s = state();
    std::lock_guard<std::mutex> lock(s.mutex);
    s.applicationId = appId ? appId : "";
}

FRUST_PLUGIN_HOST_API const char* frust_plugin_host_application_identity(void) {
    auto& s = state();
    std::lock_guard<std::mutex> lock(s.mutex);
    return s.applicationId.c_str();
}

FRUST_PLUGIN_HOST_API int64_t frust_plugin_call_on_init(FrustPluginHandle handle) {
    auto* fn = reinterpret_cast<int64_t(*)()>(frust_plugin_get_fn(handle, "on_init"));
    if (!fn) return 0;
    // See g_registeringHandle's declaration - this window is how
    // frust_register_event_handler attributes a registration made
    // during on_init() to this specific plugin.
    FrustPluginHandleImpl* prev = g_registeringHandle;
    g_registeringHandle = handle;
    int64_t result = fn();
    g_registeringHandle = prev;
    return result;
}

FRUST_PLUGIN_HOST_API int64_t frust_plugin_call_on_event(FrustPluginHandle handle, int64_t id, int64_t arg) {
    auto* fn = reinterpret_cast<int64_t(*)(int64_t, int64_t)>(frust_plugin_get_fn(handle, "on_event"));
    if (!fn) return 0;
    // Same ownership-tracking window as frust_plugin_call_on_init - a
    // plugin reacting to one event by subscribing to a new one (or
    // registering a service in response to something happening) is a
    // real, expected pattern, not just a startup-time thing.
    FrustPluginHandleImpl* prev = g_registeringHandle;
    g_registeringHandle = handle;
    int64_t result = fn(id, arg);
    g_registeringHandle = prev;
    return result;
}

FRUST_PLUGIN_HOST_API int64_t frust_plugin_call_on_unload(FrustPluginHandle handle) {
    auto* fn = reinterpret_cast<int64_t(*)()>(frust_plugin_get_fn(handle, "on_unload"));
    return fn ? fn() : 0;
}

FRUST_PLUGIN_HOST_API void frust_fire_event(const char* name, void* payload) {
    if (!name) return;
    // Copy the handler list out under the lock, then call handlers with
    // the lock released - a handler is arbitrary plugin code and could
    // itself call back into this library (e.g. register another
    // handler, or unload a plugin), which would deadlock on a
    // non-recursive std::mutex if held across the calls.
    std::vector<void(*)(void*)> handlersCopy;
    {
        auto& s = state();
        std::lock_guard<std::mutex> lock(s.mutex);
        auto it = s.eventHandlers.find(name);
        if (it != s.eventHandlers.end()) handlersCopy = it->second;
    }
    for (auto* h : handlersCopy) h(payload);
}

FRUST_PLUGIN_HOST_API void frust_register_event_handler(const char* name, void (*handler)(void*)) {
    if (!name || !handler) return;
    auto& s = state();
    std::lock_guard<std::mutex> lock(s.mutex);
    s.eventHandlers[name].push_back(handler);
    if (g_registeringHandle) {
        s.handlersByPlugin[g_registeringHandle].push_back({name, handler});
    }
}

FRUST_PLUGIN_HOST_API void frust_register_service(const char* name, void* service) {
    if (!name || !service) return;
    auto& s = state();
    std::lock_guard<std::mutex> lock(s.mutex);
    if (s.services.count(name)) {
        std::cerr << "frust_plugin_host: service '" << name << "' registered again - "
                   << "replacing the previous registration (last call wins)\n";
    }
    s.services[name] = service;
    if (g_registeringHandle) {
        s.servicesByPlugin[g_registeringHandle].push_back({name, service});
    }
}

FRUST_PLUGIN_HOST_API void* frust_lookup_service(const char* name) {
    if (!name) return nullptr;
    auto& s = state();
    std::lock_guard<std::mutex> lock(s.mutex);
    auto it = s.services.find(name);
    return it != s.services.end() ? it->second : nullptr;
}

} // extern "C"
