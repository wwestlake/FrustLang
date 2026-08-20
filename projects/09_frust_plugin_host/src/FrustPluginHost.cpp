// Implementation - see the header for the full design writeup. Mirrors
// frust_compiler's Main.cpp JIT path (RunViaJit/ParseSource) closely,
// generalized to support multiple independently loadable/unloadable
// plugins instead of one throwaway run.

#include "frust_plugin_host/FrustPluginHost.h"

#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <string>

#include "AST.h"
#include "Codegen.h"
#include "Lexer.h"
#include "ModuleLoader.h"
#include "parser.hpp"

#include <llvm/ExecutionEngine/Orc/Core.h>
#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/Support/TargetSelect.h>

using namespace frust;

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

} // extern "C"
