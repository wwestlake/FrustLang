// Frust's runtime support library - the frust_* C exports that
// `06_frust_library/core` (and the compiler's own `quote`/`unquote`/
// `build_time` codegen) declare via `extern fn` and call directly.
//
// EXTRACTED from frust_compiler's and the IDE's own Main.cpp, where this
// exact code used to be duplicated verbatim in both places (each process
// exported its own copy so LLVM ORC JIT's
// DynamicLibrarySearchGenerator::GetForCurrentProcess() could resolve
// these symbols at JIT time from whichever process happened to be
// running). That worked for JIT (`frust_compiler <file>`, the IDE's
// REPL) but gave a `frate build`-produced standalone linked executable
// no way to get these symbols at all - there was no actual .lib for its
// linker to pull them from. Building this as a real static library
// (frust_runtime) fixes both problems at once: frust_compiler.exe and
// frust_ide.exe now link this ONE copy instead of each defining their
// own, and frate's AOT link step can link frust_runtime.lib directly.
//
// __declspec(dllexport) is MSVC-only - GCC/Clang don't recognize it at
// all (a hard compile error, not a harmless no-op). FRUST_RUNTIME_EXPORT
// is the portable equivalent: dllexport on Windows, explicit default
// visibility on Linux (matches ELF's default for extern "C" symbols
// anyway, but stated explicitly so it stays correct even if this file is
// ever built with -fvisibility=hidden).

#include <atomic>
#include <cstdio>
#include <iostream>
#include <mutex>

#include "Codegen.h"

#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/ExecutionEngine/Orc/ExecutionUtils.h>
#include <llvm/Support/TargetSelect.h>

namespace {
// frust_compiler's own Main.cpp calls these once, itself, before doing
// anything JIT-related - but that's Main.cpp's own main(), which a real
// AOT-linked Frust executable (frate build's output) never runs at all;
// its own "main" is the compiled Frust program's main fn. Anything in
// this file that touches LLVM's JIT has to be safe to call from EITHER
// context, so it's self-initializing here instead of assuming the
// caller already did it.
void ensureLlvmNativeTargetInitialized() {
    static std::once_flag flag;
    std::call_once(flag, [] {
        llvm::InitializeNativeTarget();
        llvm::InitializeNativeTargetAsmPrinter();
        llvm::InitializeNativeTargetAsmParser();
    });
}
} // namespace

#if defined(_WIN32)
#define FRUST_RUNTIME_EXPORT extern "C" __declspec(dllexport)
#else
#define FRUST_RUNTIME_EXPORT extern "C" __attribute__((visibility("default")))
#endif

using namespace frust;

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
// to a fresh Codegen/LLJIT pipeline, and calls the result - the whole
// generate-compile-run loop happening inside the already-running
// process, no rebuild, no restart. Works identically whether the calling
// process got here via JIT (frust_compiler <file>) or as a real AOT
// executable linked against this library - either way, this function
// itself always JITs the generated code on the spot.
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
    ensureLlvmNativeTargetInitialized();

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
