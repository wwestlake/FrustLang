// Frust front-end driver: parses a .fr file (or, with no arguments, a
// line-at-a-time REPL), prints the resulting AST, and - when codegen
// supports everything used - JIT-compiles and actually runs it via LLVM
// OrcJIT. File mode looks for and calls a zero-arg `main`; REPL mode
// evaluates each bare top-level statement immediately and prints its value.
//
// REPL note: each line gets a fresh AstArena/Module/JIT instance, so `let`
// bindings do NOT persist across lines yet - that needs either a shared
// JITDylib-wide symbol per binding or a persistent module, which is a
// follow-up, not part of this first JIT pass.

#include <atomic>
#include <cstdio>
#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>

#if defined(_WIN32)
#include <io.h>
#endif

#include "Codegen.h"
#include "Lexer.h"
#include "ModuleLoader.h"
#include "ReplSession.h"
#include "parser.hpp"

#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/ExecutionEngine/Orc/ExecutionUtils.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Support/Error.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/TargetParser/Host.h>

namespace frust {
namespace {

bool canUseStdinForRepl() {
#if defined(_WIN32)
    const auto fd = _fileno(stdin);
    return fd >= 0;
#else
    return true;
#endif
}

frust::Program* ParseSource(std::istream& input, AstArena& arena, std::vector<std::string>& parseErrors) {
    Lexer lexer(&input);
    Program* result = nullptr;
    std::vector<ParseError> structuredErrors; // unused here - frust_lsp has its own ParseSource that reads these
    Parser parser(lexer, arena, parseErrors, result, structuredErrors);
    parser.parse();
    parseErrors.insert(parseErrors.end(), lexer.errors.begin(), lexer.errors.end());
    if (result) ResolveImports(result, arena, parseErrors);
    return result;
}

// Parses every file in `paths` into the same arena and merges their
// top-level decls into one Program - this is what lets a single pod's
// source live in multiple named files (math.fr, console_io.fr, ...)
// instead of one big lib.fr; they're compiled together as one unit, same
// as if it had all been one file. Collects parse errors across every file
// before the caller checks `!parseErrors.empty()`, rather than stopping
// at the first failing file, so a multi-file error report shows everything
// wrong in one pass.
frust::Program* ParseAndMergeFiles(const std::vector<std::string>& paths, AstArena& arena, std::vector<std::string>& parseErrors) {
    Program* merged = arena.NewProgram();
    for (const auto& path : paths) {
        std::ifstream file(path);
        if (!file) {
            parseErrors.push_back("frust: cannot open '" + path + "'");
            continue;
        }
        Program* prog = ParseSource(file, arena, parseErrors);
        if (!prog) continue;
        merged->decls.insert(merged->decls.end(), prog->decls.begin(), prog->decls.end());
    }
    return merged;
}

std::string SmartPtrKindName(SmartPtrKind kind) {
    switch (kind) {
        case SmartPtrKind::Own: return "own";
        case SmartPtrKind::Shared: return "shared";
        case SmartPtrKind::Weak: return "weak";
        case SmartPtrKind::Raw: return "raw*";
        case SmartPtrKind::Ref: return "&";
        default: return "";
    }
}

void PrintIndent(int depth) {
    for (int i = 0; i < depth; ++i) std::cout << "  ";
}

void PrintType(const TypeExpr* type, int depth) {
    if (!type) { std::cout << "<inferred>"; return; }
    if (type->ptrKind != SmartPtrKind::None) std::cout << SmartPtrKindName(type->ptrKind) << " ";
    std::cout << type->name;
    if (!type->genericArgs.empty()) {
        std::cout << "<";
        for (size_t i = 0; i < type->genericArgs.size(); ++i) {
            if (i > 0) std::cout << ", ";
            auto& arg = type->genericArgs[i];
            if (arg.isIntConst) std::cout << arg.intConst;
            else PrintType(arg.type, depth);
        }
        std::cout << ">";
    }
    if (type->refinementKind == RefinementKind::Range) {
        std::cout << "[" << type->refLow << " .. " << type->refHigh << "]";
    } else if (type->refinementKind == RefinementKind::Compare) {
        std::cout << "[cmp " << type->refCmpValue << "]";
    }
}

const char* ExprKindName(ExprKind kind) {
    switch (kind) {
        case ExprKind::IntLiteral: return "IntLiteral";
        case ExprKind::FloatLiteral: return "FloatLiteral";
        case ExprKind::BoolLiteral: return "BoolLiteral";
        case ExprKind::StringLiteral: return "StringLiteral";
        case ExprKind::Identifier: return "Identifier";
        case ExprKind::Path: return "Path";
        case ExprKind::Unary: return "Unary";
        case ExprKind::Binary: return "Binary";
        case ExprKind::Assign: return "Assign";
        case ExprKind::Cast: return "Cast";
        case ExprKind::Call: return "Call";
        case ExprKind::Member: return "Member";
        case ExprKind::Index: return "Index";
        case ExprKind::SmartPtrNew: return "SmartPtrNew";
        case ExprKind::StructLiteral: return "StructLiteral";
        case ExprKind::Block: return "Block";
        case ExprKind::Let: return "Let";
        case ExprKind::Return: return "Return";
        case ExprKind::If: return "If";
        case ExprKind::BuildTime: return "BuildTime";
        case ExprKind::Quote: return "Quote";
        case ExprKind::Unquote: return "Unquote";
        case ExprKind::Perform: return "Perform";
        case ExprKind::Resume: return "Resume";
        case ExprKind::Handle: return "Handle";
        case ExprKind::Closure: return "Closure";
    }
    return "?";
}

void PrintExpr(const Expr* expr, int depth) {
    if (!expr) { PrintIndent(depth); std::cout << "<null>\n"; return; }
    PrintIndent(depth);
    std::cout << ExprKindName(expr->kind);

    switch (expr->kind) {
        case ExprKind::IntLiteral: std::cout << " " << expr->intValue; break;
        case ExprKind::FloatLiteral: std::cout << " " << expr->floatValue; break;
        case ExprKind::BoolLiteral: std::cout << " " << (expr->boolValue ? "true" : "false"); break;
        case ExprKind::StringLiteral: std::cout << " \"" << expr->text << "\""; break;
        case ExprKind::Identifier: std::cout << " " << expr->text; break;
        case ExprKind::Path: {
            std::cout << " ";
            for (size_t i = 0; i < expr->pathSegments.size(); ++i) {
                if (i > 0) std::cout << "::";
                std::cout << expr->pathSegments[i];
            }
            break;
        }
        case ExprKind::Member: std::cout << " ." << expr->text; break;
        case ExprKind::Let: std::cout << " " << (expr->isMut ? "mut " : "") << expr->text; break;
        case ExprKind::Perform: std::cout << " " << expr->text; break;
        case ExprKind::SmartPtrNew: std::cout << " " << SmartPtrKindName(expr->smartPtrKind); break;
        case ExprKind::StructLiteral: {
            std::cout << " ";
            for (size_t i = 0; i < expr->pathSegments.size(); ++i) {
                if (i > 0) std::cout << "::";
                std::cout << expr->pathSegments[i];
            }
            break;
        }
        default: break;
    }
    std::cout << "\n";

    if (expr->typeAnnotation) { PrintIndent(depth + 1); std::cout << "type: "; PrintType(expr->typeAnnotation, depth + 1); std::cout << "\n"; }
    if (expr->condExpr) PrintExpr(expr->condExpr, depth + 1);
    if (expr->lhs) PrintExpr(expr->lhs, depth + 1);
    if (expr->rhs) PrintExpr(expr->rhs, depth + 1);
    if (expr->elseExpr) PrintExpr(expr->elseExpr, depth + 1);
    for (auto* arg : expr->args) PrintExpr(arg, depth + 1);
    for (auto* stmt : expr->statements) PrintExpr(stmt, depth + 1);
    for (auto& field : expr->fields) {
        PrintIndent(depth + 1); std::cout << "field " << field.name << ":\n";
        PrintExpr(field.value, depth + 2);
    }
    for (auto& hc : expr->handleCases) {
        PrintIndent(depth + 1); std::cout << "effect " << hc.effectName << "(";
        for (size_t i = 0; i < hc.params.size(); ++i) { if (i > 0) std::cout << ", "; std::cout << hc.params[i].name; }
        std::cout << ") =>\n";
        PrintExpr(hc.body, depth + 2);
    }
    if (expr->kind == ExprKind::Closure && !expr->params.empty()) {
        PrintIndent(depth + 1); std::cout << "params:\n";
        for (auto& p : expr->params) {
            PrintIndent(depth + 2);
            std::cout << p.name;
            if (p.type) { std::cout << ": "; PrintType(p.type, depth + 2); }
            std::cout << "\n";
        }
    }
}

void PrintParams(const std::vector<Param>& params, int depth) {
    for (auto& p : params) {
        PrintIndent(depth);
        std::cout << p.name;
        if (p.type) { std::cout << ": "; PrintType(p.type, depth); }
        std::cout << "\n";
    }
}

void PrintDecl(const Decl* decl, int depth) {
    PrintIndent(depth);
    switch (decl->kind) {
        case DeclKind::Function: {
            auto* f = decl->functionDecl;
            std::cout << "fn " << (f->isPub ? "pub " : "") << (f->isUnsafe ? "unsafe " : "") << f->name << "(...)";
            if (f->returnType) { std::cout << " -> "; PrintType(f->returnType, depth); }
            std::cout << "\n";
            PrintIndent(depth + 1); std::cout << "params:\n"; PrintParams(f->params, depth + 2);
            if (f->body) { PrintIndent(depth + 1); std::cout << "body:\n"; PrintExpr(f->body, depth + 2); }
            else { PrintIndent(depth + 1); std::cout << "(prototype, no body)\n"; }
            break;
        }
        case DeclKind::Struct: {
            auto* s = decl->structDecl;
            std::cout << "struct " << s->name << "\n";
            for (auto& field : s->fields) {
                PrintIndent(depth + 1); std::cout << field.name << ": "; PrintType(field.type, depth + 1); std::cout << "\n";
            }
            break;
        }
        case DeclKind::TypeAlias: {
            auto* t = decl->typeAliasDecl;
            std::cout << "type " << t->name << " = "; PrintType(t->aliasedType, depth); std::cout << "\n";
            break;
        }
        case DeclKind::Effect: {
            auto* e = decl->effectDecl;
            std::cout << "effect " << e->name << "(...)";
            if (e->returnType) { std::cout << " -> "; PrintType(e->returnType, depth); }
            std::cout << "\n";
            PrintParams(e->params, depth + 1);
            break;
        }
        case DeclKind::TopLevelStmt: {
            std::cout << "(top-level statement)\n";
            PrintExpr(decl->topLevelStmt, depth + 1);
            break;
        }
        case DeclKind::Component: {
            auto* c = decl->componentDecl;
            std::cout << "component " << c->name << "(...)";
            if (!c->interfaceName.empty()) std::cout << " : " << c->interfaceName;
            std::cout << "\n";
            PrintIndent(depth + 1); std::cout << "params:\n"; PrintParams(c->params, depth + 2);
            for (auto& port : c->ports) {
                PrintIndent(depth + 1);
                std::cout << (port.isOutput ? "out " : "in ") << port.name;
                if (port.type) { std::cout << ": "; PrintType(port.type, depth + 1); }
                std::cout << "\n";
            }
            for (auto* stmt : c->bodyStatements) PrintExpr(stmt, depth + 1);
            break;
        }
    }
}

const FunctionDecl* FindEntryPoint(const Program& prog) {
    const FunctionDecl* entryFn = nullptr;
    for (auto* decl : prog.decls) {
        if (decl->kind == DeclKind::Function && decl->functionDecl->isEntry && decl->functionDecl->body != nullptr) {
            if (entryFn != nullptr) {
                std::cerr << "frust: error: multiple functions marked with 'entry'\n";
                return nullptr;
            }
            entryFn = decl->functionDecl;
        }
    }
    
    if (entryFn) return entryFn;

    // Fallback for backwards compatibility
    for (auto* decl : prog.decls) {
        if (decl->kind == DeclKind::Function && decl->functionDecl->name == "main" && decl->functionDecl->body != nullptr) {
            return decl->functionDecl;
        }
    }
    
    return nullptr;
}

// Calls a zero-arg JIT'd function through the right C++ function-pointer
// type for its declared Frust return type, so the ABI actually matches what
// codegen emitted (resolveType()'s exact mapping) rather than guessing.
void CallAndPrint(const FunctionDecl& fn, void* addr) {
    if (!fn.returnType) {
        reinterpret_cast<void (*)()>(addr)();
        std::cout << fn.name << "() ran (no return value)\n";
        return;
    }

    const std::string& retName = fn.returnType->name;
    if (retName == "f32") {
        std::cout << fn.name << "() => " << reinterpret_cast<float (*)()>(addr)() << "\n";
    } else if (retName == "f64") {
        std::cout << fn.name << "() => " << reinterpret_cast<double (*)()>(addr)() << "\n";
    } else if (retName == "bool") {
        std::cout << fn.name << "() => " << (reinterpret_cast<bool (*)()>(addr)() ? "true" : "false") << "\n";
    } else if (retName == "i8" || retName == "u8") {
        std::cout << fn.name << "() => " << static_cast<int>(reinterpret_cast<int8_t (*)()>(addr)()) << "\n";
    } else if (retName == "i16" || retName == "u16") {
        std::cout << fn.name << "() => " << reinterpret_cast<int16_t (*)()>(addr)() << "\n";
    } else if (retName == "i32" || retName == "u32") {
        std::cout << fn.name << "() => " << reinterpret_cast<int32_t (*)()>(addr)() << "\n";
    } else {
        // i64/u64/usize/isize, plus anything codegen didn't recognize (it
        // already defaulted those to i64 with a warning at codegen time).
        std::cout << fn.name << "() => " << reinterpret_cast<int64_t (*)()>(addr)() << "\n";
    }
}

// Runtime backing for the frust-library `core` pod (extern-declared there,
// matching these exact signatures/names). Exported from this process's own
// image so LLVM ORC JIT's default
// DynamicLibrarySearchGenerator::GetForCurrentProcess() resolves them at
// JIT time - works for `frust_compiler <file>` and the IDE's REPL (which
// needs the identical set exported from ITS OWN process image too, see
// projects/02_juce_language_host/Source/Main.cpp - keep both in sync).
//
// NOT yet usable from a `frate build`-produced standalone linked
// executable - that needs an actual runtime .lib for the linker to pull
// these symbols from, which doesn't exist yet (AOT/"hard iron mode"
// compilation is explicitly deferred, same as FRATE_SPEC.md already notes
// for frate build in general).
//
// __declspec(dllexport) is MSVC-only - GCC/Clang don't recognize it at
// all (a hard compile error, not a harmless no-op). FRUST_RUNTIME_EXPORT
// is the portable equivalent: dllexport on Windows, explicit default
// visibility on Linux (matches ELF's default for extern "C" symbols
// anyway, but stated explicitly so it stays correct even if this file is
// ever built with -fvisibility=hidden).
#if defined(_WIN32)
#define FRUST_RUNTIME_EXPORT extern "C" __declspec(dllexport)
#else
#define FRUST_RUNTIME_EXPORT extern "C" __attribute__((visibility("default")))
#endif

FRUST_RUNTIME_EXPORT void frust_print_f64(double val) {
    std::cout << val << "\n";
}
FRUST_RUNTIME_EXPORT void frust_print_str(const char* val) {
    std::cout << val << "\n";
}

// Formatting backs core's format_*/concat functions, which println_i64/
// f64/bool are themselves built from (format -> String, then
// println_str) rather than each having their own direct-print C export.
// Frust has no string ownership/allocation of its own yet, so these hand
// back a pointer into a small rotating pool of static buffers rather than
// a heap allocation - good enough for "format a couple of values and
// concat/print them in one expression," NOT safe to stash a returned
// String past a handful of further format/concat calls (same class of
// caveat as C's strtok/ctime). kFormatBufferCount is comfortably more than
// any realistic single expression's nesting depth.
namespace {
constexpr int kFormatBufferCount = 16;
// Was 64 - too small for realistic use (a real absolute Windows path
// alone silently truncated inside frust_str_concat's snprintf, causing
// a downstream "cannot open" on the truncated path during
// process_task.fr's multiprocessing work). 512 gives real headroom for
// paths/messages while staying a trivial per-slot cost (16 slots * 512
// bytes = 8KB thread_local, negligible).
constexpr size_t kFormatBufferSize = 512;
thread_local char formatBufferPool[kFormatBufferCount][kFormatBufferSize];
thread_local int formatBufferIndex = 0;

char* nextFormatBuffer() {
    char* buf = formatBufferPool[formatBufferIndex];
    formatBufferIndex = (formatBufferIndex + 1) % kFormatBufferCount;
    return buf;
}
} // namespace

FRUST_RUNTIME_EXPORT const char* frust_format_i64(int64_t val) {
    char* buf = nextFormatBuffer();
    std::snprintf(buf, kFormatBufferSize, "%lld", static_cast<long long>(val));
    return buf;
}
FRUST_RUNTIME_EXPORT const char* frust_format_f64(double val) {
    char* buf = nextFormatBuffer();
    std::snprintf(buf, kFormatBufferSize, "%g", val);
    return buf;
}
FRUST_RUNTIME_EXPORT const char* frust_format_bool(bool val) {
    return val ? "true" : "false"; // static literals - always valid, no pool slot needed
}
FRUST_RUNTIME_EXPORT const char* frust_str_concat(const char* a, const char* b) {
    char* buf = nextFormatBuffer();
    std::snprintf(buf, kFormatBufferSize, "%s%s", a, b);
    return buf;
}

// Indexed scalar read/write into an arbitrary buffer (e.g. one returned
// by mem.fr's alloc()). This is the one primitive Frust's own codegen is
// still missing (no pointer-dereference/indexed-write support - see
// UnaryOp::Deref's "not supported yet" branch in Codegen.h, and
// compileAssign only handling struct-field LHS, not Index), so it's
// exposed the same way frust_format_*/frust_print_str already are: a
// small fixed-arity C helper a Frust `extern fn` can call directly.
// idx is an element index (not a byte offset) - matches core/mem.fr's
// existing alloc()-returns-a-buffer convention. No bounds checking, same
// "caller's problem" stance as raw C pointer arithmetic.
FRUST_RUNTIME_EXPORT int64_t frust_buf_get_i64(const int64_t* base, int64_t idx) {
    return base[idx];
}
FRUST_RUNTIME_EXPORT void frust_buf_set_i64(int64_t* base, int64_t idx, int64_t val) {
    base[idx] = val;
}

// Pointer-slot sibling of the above - lets a Frust buffer hold other
// pointers (another buffer's address, a struct's address, etc.) without
// needing a pointer<->i64 cast Frust's coerceToType doesn't implement.
// Exists specifically so a struct constructed in one function's stack
// frame can be safely handed off to a spawned thread that outlives that
// frame: pack its would-be fields into a heap buffer via this instead.
FRUST_RUNTIME_EXPORT void* frust_buf_get_ptr(void* const* base, int64_t idx) {
    return base[idx];
}
FRUST_RUNTIME_EXPORT void frust_buf_set_ptr(void** base, int64_t idx, void* val) {
    base[idx] = val;
}

// ---------------------------------------------------------------------
// Live AST metaprogramming runtime (FRUST_LANG_SPEC.md 1.1: `quote`/
// `unquote`/`build_time`). Codegen.h's buildQuoteTree() compiles a
// `quote { ... }` block into calls to the frust_ast_* functions below -
// so when a running Frust program actually executes a quote block, it
// builds a REAL frust::Expr tree, live, using whatever values are live
// at that moment (unquote's operand is ordinary compiled code, not
// something resolved ahead of time). frust_jit_eval_f32 is the "run it
// now" primitive: wraps that tree as a one-parameter function, hands it
// to the SAME Codegen/LLJIT pipeline RunViaJit already uses, and calls
// the result - the whole generate-compile-run loop happening inside the
// already-running process, no rebuild, no restart.
namespace {

// Backs every runtime-constructed quote-tree node for the life of the
// process. Deliberately never freed - v1 scope, same as several other
// process-lifetime runtime structures here. Revisit with a real
// reclaim/arena-per-call strategy if a program ends up calling this in
// a hot loop; for the "generate a specialized function occasionally, in
// response to something changing" use case this is built for, the
// volume is inherently bounded.
AstArena& quoteRuntimeArena() {
    static AstArena arena;
    return arena;
}

std::atomic<uint64_t> jitEvalCounter{0};

} // namespace

FRUST_RUNTIME_EXPORT void* frust_ast_lit_f64(double val) {
    Expr* e = quoteRuntimeArena().NewExpr(ExprKind::FloatLiteral, SourceLoc{});
    e->floatValue = val;
    return e;
}

FRUST_RUNTIME_EXPORT void* frust_ast_param(const char* name) {
    Expr* e = quoteRuntimeArena().NewExpr(ExprKind::Identifier, SourceLoc{});
    e->text = name ? name : "";
    return e;
}

FRUST_RUNTIME_EXPORT void* frust_ast_binary(int32_t opCode, void* lhs, void* rhs) {
    Expr* e = quoteRuntimeArena().NewExpr(ExprKind::Binary, SourceLoc{});
    e->binaryOp = static_cast<BinaryOp>(opCode);
    e->lhs = reinterpret_cast<Expr*>(lhs);
    e->rhs = reinterpret_cast<Expr*>(rhs);
    return e;
}

FRUST_RUNTIME_EXPORT void* frust_ast_unary(int32_t opCode, void* operand) {
    Expr* e = quoteRuntimeArena().NewExpr(ExprKind::Unary, SourceLoc{});
    e->unaryOp = static_cast<UnaryOp>(opCode);
    e->lhs = reinterpret_cast<Expr*>(operand);
    return e;
}

// The "run it now" primitive. `astPtr` is a tree already fully built by
// the calls above (built bottom-up, so by the time this runs every
// node's children are already populated) - wraps it as the body of a
// synthesized one-f32-parameter function named "x" (the spec's own
// example's convention - see the plan's stated v1 scope: full generality
// needs function-pointer/closure support Frust doesn't have yet, so this
// makes the actual call itself, in C++, with a fixed signature), compiles
// and JITs it fresh, and calls it immediately.
FRUST_RUNTIME_EXPORT float frust_jit_eval_f32(void* astPtr, float x) {
    if (!astPtr) {
        std::cerr << "frust: frust_jit_eval_f32 called with a null ASTExpr\n";
        return 0.0f;
    }
    Expr* treeRoot = reinterpret_cast<Expr*>(astPtr);

    AstArena& arena = quoteRuntimeArena();
    SourceLoc loc;

    TypeExpr* f32Type = arena.NewType(loc);
    f32Type->name = "f32";

    Param xParam;
    xParam.name = "x";
    xParam.type = f32Type;
    xParam.loc = loc;

    FunctionDecl* fn = arena.NewFunctionDecl();
    fn->name = "__frust_jit_gen_" + std::to_string(jitEvalCounter.fetch_add(1));
    fn->isPub = true;
    fn->params.push_back(xParam);
    fn->returnType = f32Type;
    fn->body = treeRoot;

    Decl* decl = arena.NewDecl(DeclKind::Function);
    decl->functionDecl = fn;

    Program* prog = arena.NewProgram();
    prog->decls.push_back(decl);

    auto genContext = std::make_unique<llvm::LLVMContext>();
    auto genModule = std::make_unique<llvm::Module>("frust_jit_eval", *genContext);

    Codegen codegen(*genContext, *genModule);
    if (!codegen.compileProgram(*prog)) {
        std::cerr << "frust: frust_jit_eval_f32: generated code failed codegen\n";
        return 0.0f;
    }

    auto jitOrErr = llvm::orc::LLJITBuilder().create();
    if (!jitOrErr) {
        std::cerr << "frust: frust_jit_eval_f32: JIT init failed: " << llvm::toString(jitOrErr.takeError()) << "\n";
        return 0.0f;
    }
    auto jit = std::move(*jitOrErr);

    auto generator = llvm::orc::DynamicLibrarySearchGenerator::GetForCurrentProcess(jit->getDataLayout().getGlobalPrefix());
    if (generator) jit->getMainJITDylib().addGenerator(std::move(*generator));

    llvm::orc::ThreadSafeModule tsm(std::move(genModule), std::move(genContext));
    if (auto err = jit->addIRModule(std::move(tsm))) {
        std::cerr << "frust: frust_jit_eval_f32: JIT module load failed: " << llvm::toString(std::move(err)) << "\n";
        return 0.0f;
    }

    auto sym = jit->lookup(fn->name);
    if (!sym) {
        std::cerr << "frust: frust_jit_eval_f32: JIT lookup failed: " << llvm::toString(sym.takeError()) << "\n";
        return 0.0f;
    }

    auto fnPtr = sym->toPtr<float(*)(float)>();
    return fnPtr(x);
}

void optimizeModule(llvm::Module& M) {
    llvm::LoopAnalysisManager LAM;
    llvm::FunctionAnalysisManager FAM;
    llvm::CGSCCAnalysisManager CGAM;
    llvm::ModuleAnalysisManager MAM;

    llvm::PassBuilder PB;
    PB.registerModuleAnalyses(MAM);
    PB.registerCGSCCAnalyses(CGAM);
    PB.registerFunctionAnalyses(FAM);
    PB.registerLoopAnalyses(LAM);
    PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);

    {
        std::error_code EC;
        llvm::raw_fd_ostream dest("output_pre_opt.ll", EC, llvm::sys::fs::OF_None);
        M.print(dest, nullptr);
    }

    llvm::ModulePassManager MPM = PB.buildPerModuleDefaultPipeline(llvm::OptimizationLevel::O2);
    std::cout << "--- Running LLVM Pass Manager ---" << std::endl << std::flush;
    MPM.run(M, MAM);
    std::cout << "--- Pass Manager Finished ---" << std::endl << std::flush;
    
    {
        std::error_code EC;
        llvm::raw_fd_ostream dest("output_post_opt.ll", EC, llvm::sys::fs::OF_None);
        M.print(dest, nullptr);
    }
}

// Builds an LLJIT, hands it the module, looks up `symbolName`, and calls
// `onFound` with the resolved address. Prints and returns on any failure.
void RunViaJit(std::unique_ptr<llvm::LLVMContext> context, std::unique_ptr<llvm::Module> module,
                const std::string& symbolName, const std::function<void(void*)>& onFound) {
    auto jitOrErr = llvm::orc::LLJITBuilder().create();
    if (!jitOrErr) {
        std::cerr << "frust: JIT init failed: " << llvm::toString(jitOrErr.takeError()) << "\n";
        return;
    }
    auto jit = std::move(*jitOrErr);

    auto generator = llvm::orc::DynamicLibrarySearchGenerator::GetForCurrentProcess(
        jit->getDataLayout().getGlobalPrefix());
    if (generator) {
        jit->getMainJITDylib().addGenerator(std::move(*generator));
    }

    optimizeModule(*module);

    llvm::orc::ThreadSafeModule tsm(std::move(module), std::move(context));
    if (auto err = jit->addIRModule(std::move(tsm))) {
        std::cerr << "frust: JIT module load failed: " << llvm::toString(std::move(err)) << "\n";
        return;
    }

    auto sym = jit->lookup(symbolName);
    if (!sym) {
        std::cerr << "frust: JIT lookup of '" << symbolName << "' failed: " << llvm::toString(sym.takeError()) << "\n";
        return;
    }

    onFound(sym->toPtr<void*>());
}

void RunFile(const std::vector<std::string>& paths) {
    AstArena arena;
    std::vector<std::string> parseErrors;
    Program* prog = ParseAndMergeFiles(paths, arena, parseErrors);

    if (!parseErrors.empty() || !prog) {
        std::cerr << "frust: " << parseErrors.size() << " error(s), aborting\n";
        for (const auto& err : parseErrors) std::cerr << err << "\n";
        return;
    }

    std::cout << "Program (" << prog->decls.size() << " declaration(s)):\n";
    for (auto* decl : prog->decls) PrintDecl(decl, 1);

    auto* mainFn = FindEntryPoint(*prog);
    if (!mainFn) {
        std::cout << "\n(no 'entry' function or zero-arg 'main' to run)\n";
        return;
    }
    if (!mainFn->params.empty()) {
        std::cout << "\n(entry function takes parameters - JIT auto-run only supports zero-arg entry for now)\n";
        return;
    }

    auto context = std::make_unique<llvm::LLVMContext>();
    auto module = std::make_unique<llvm::Module>("FrustModule", *context);

    Codegen codegen(*context, *module);
    if (!codegen.compileProgram(*prog)) {
        std::cerr << "\nfrust: codegen failed, not running\n";
        return;
    }

    {
        std::error_code EC;
        llvm::raw_fd_ostream dest("output.ll", EC, llvm::sys::fs::OF_None);
        module->print(dest, nullptr);
    }

    std::cout << "\nRunning program...\n";
    RunViaJit(std::move(context), std::move(module), mainFn->name, [mainFn](void* addr) {
        CallAndPrint(*mainFn, addr);
    });
}

void CompileToObject(const std::vector<std::string>& paths, const std::string& outputPath) {
    AstArena arena;
    std::vector<std::string> parseErrors;
    Program* prog = ParseAndMergeFiles(paths, arena, parseErrors);

    if (!parseErrors.empty() || !prog) {
        std::cerr << "frust: " << parseErrors.size() << " error(s), aborting\n";
        for (const auto& err : parseErrors) std::cerr << err << "\n";
        return;
    }

    auto context = std::make_unique<llvm::LLVMContext>();
    auto module = std::make_unique<llvm::Module>("FrustModule", *context);

    Codegen codegen(*context, *module);
    if (!codegen.compileProgram(*prog)) {
        std::cerr << "\nfrust: codegen failed, not emitting object\n";
        return;
    }
    
    optimizeModule(*module);

    auto TargetTriple = llvm::sys::getDefaultTargetTriple();
    module->setTargetTriple(TargetTriple);

    std::string Error;
    auto Target = llvm::TargetRegistry::lookupTarget(TargetTriple, Error);

    if (!Target) {
        llvm::errs() << Error;
        return;
    }

    auto CPU = "generic";
    auto Features = "";
    llvm::TargetOptions opt;
    auto RM = std::optional<llvm::Reloc::Model>();
    auto TheTargetMachine = Target->createTargetMachine(TargetTriple, CPU, Features, opt, RM);

    module->setDataLayout(TheTargetMachine->createDataLayout());

    std::error_code EC;
    llvm::raw_fd_ostream dest(outputPath, EC, llvm::sys::fs::OF_None);

    if (EC) {
        llvm::errs() << "Could not open file: " << EC.message();
        return;
    }

    llvm::legacy::PassManager pass;
    auto FileType = llvm::CodeGenFileType::ObjectFile;

    if (TheTargetMachine->addPassesToEmitFile(pass, dest, nullptr, FileType)) {
        llvm::errs() << "TheTargetMachine can't emit a file of this type";
        return;
    }

    pass.run(*module);
    dest.flush();
}

void RunRepl() {
    std::cout << "=======================================================\n";
    std::cout << " Frust v0.1.0 - Parse/AST REPL (flex/bison front end)\n";
    std::cout << " Type a declaration or expression. :quit to exit.\n";
    std::cout << "=======================================================\n\n";

    // Reused across lines for the AST-printing debug view below; actual
    // evaluation goes through ReplSession (same class the JUCE host's
    // console panel calls), not a second hand-rolled copy of this logic.
    ReplSession session;

    std::string line;
    while (true) {
        std::cout << "frust> ";
        if (!std::getline(std::cin, line) || line == ":quit") break;
        if (line.empty()) continue;

        std::istringstream input(line);
        AstArena arena;
        std::vector<std::string> parseErrors;
        Program* prog = ParseSource(input, arena, parseErrors);

        if (!parseErrors.empty() || !prog) {
            std::cout << "  (" << parseErrors.size() << " error(s))\n";
            for (const auto& err : parseErrors) std::cout << "  " << err << "\n";
            continue;
        }

        for (auto* decl : prog->decls) PrintDecl(decl, 1);

        if (prog->decls.empty() || prog->decls.back()->kind != DeclKind::TopLevelStmt) continue;

        std::cout << "  => " << session.evaluate(line) << "\n";
    }
}

} // namespace
} // namespace frust

int main(int argc, char** argv) {
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();
    llvm::InitializeNativeTargetAsmParser();

    // --emit-obj takes the output path first (always exactly one) followed
    // by one or more input .fr files, which get parsed and merged into a
    // single compiled unit - this is what lets a pod's source span
    // multiple named files (math.fr, console_io.fr, ...) instead of one
    // big lib.fr. Plain-run mode (no --emit-obj) takes the same "one or
    // more input files" shape, just without an output path.
    if (argc >= 4 && std::string(argv[1]) == "--emit-obj") {
        std::string outputFile = argv[2];
        std::vector<std::string> inputFiles(argv + 3, argv + argc);
        frust::CompileToObject(inputFiles, outputFile);
    } else if (argc >= 2) {
        std::vector<std::string> inputFiles(argv + 1, argv + argc);
        frust::RunFile(inputFiles);
    } else {
        if (!frust::canUseStdinForRepl()) {
            std::cerr << "frust: no input files were provided, and stdin is not available for REPL mode\n";
            return 1;
        }
        frust::RunRepl();
    }
    return 0;
}
