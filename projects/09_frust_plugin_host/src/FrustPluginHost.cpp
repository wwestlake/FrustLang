// Implementation - see the header for the full design writeup. Mirrors
// frust_compiler's Main.cpp JIT path (RunViaJit/ParseSource) closely,
// generalized to support multiple independently loadable/unloadable
// plugins instead of one throwaway run.

#include "frust_plugin_host/FrustPluginHost.h"

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

} // namespace

struct FrustPluginHandleImpl {
    std::string path;
    std::string dylibName;
    llvm::orc::JITDylib* dylib = nullptr;
};

extern "C" {

FRUST_PLUGIN_HOST_API FrustPluginHandle frust_plugin_load(const char* path) {
    auto& s = state();
    std::lock_guard<std::mutex> lock(s.mutex);
    if (!s.ensureInit()) return nullptr;

    std::ifstream file(path);
    if (!file) {
        std::cerr << "frust_plugin_host: cannot open '" << path << "'\n";
        return nullptr;
    }

    AstArena arena;
    std::vector<std::string> parseErrors;
    Program* prog = ParsePluginSource(file, arena, parseErrors);
    if (!parseErrors.empty() || !prog) {
        std::cerr << "frust_plugin_host: " << parseErrors.size() << " error(s) loading '" << path << "'\n";
        for (const auto& err : parseErrors) std::cerr << "  " << err << "\n";
        return nullptr;
    }

    std::string astHash = computeAstHash(*prog);

    auto context = std::make_unique<llvm::LLVMContext>();
    auto module = std::make_unique<llvm::Module>(path, *context);

    Codegen codegen(*context, *module);
    if (!codegen.compileProgram(*prog)) {
        std::cerr << "frust_plugin_host: codegen failed for '" << path << "'\n";
        return nullptr;
    }

    std::string dylibName = "frust_plugin#" + std::to_string(s.nextPluginId++) + ":" + path;
    auto jdOrErr = s.jit->createJITDylib(dylibName);
    if (!jdOrErr) {
        std::cerr << "frust_plugin_host: failed to create JITDylib for '" << path << "'\n";
        return nullptr;
    }
    llvm::orc::JITDylib& JD = *jdOrErr;
    JD.addToLinkOrder(*s.hostDylib);

    llvm::orc::ThreadSafeContext tsc(std::move(context));
    llvm::orc::ThreadSafeModule tsm(std::move(module), tsc);
    if (auto err = s.jit->addIRModule(JD, std::move(tsm))) {
        std::cerr << "frust_plugin_host: JIT module load failed for '" << path << "': "
                   << llvm::toString(std::move(err)) << "\n";
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
    return handle;
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

    if (s.jit && handle->dylib) {
        if (auto err = s.jit->getExecutionSession().removeJITDylib(*handle->dylib)) {
            std::cerr << "frust_plugin_host: unload of '" << handle->path << "' failed: "
                       << llvm::toString(std::move(err)) << "\n";
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
    return frust_plugin_load(path.c_str());
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
        std::cerr << "frust_plugin_host: failed to register host function '" << name << "': "
                   << llvm::toString(std::move(err)) << "\n";
    }
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
    return fn ? fn(id, arg) : 0;
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

} // extern "C"
