// LLVM-backed runtime support for quote/unquote/build_time. This stays in a
// separate object file so ordinary AOT programs do not need LLVM to link.

#include <atomic>
#include <iostream>
#include <mutex>

#include "Codegen.h"

#include <llvm/ExecutionEngine/Orc/ExecutionUtils.h>
#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/Support/TargetSelect.h>

#if defined(_WIN32)
#define FRUST_RUNTIME_EXPORT extern "C" __declspec(dllexport)
#else
#define FRUST_RUNTIME_EXPORT extern "C" __attribute__((visibility("default")))
#endif

using namespace frust;

namespace {
void ensureLlvmNativeTargetInitialized() {
    static std::once_flag flag;
    std::call_once(flag, [] {
        llvm::InitializeNativeTarget();
        llvm::InitializeNativeTargetAsmPrinter();
        llvm::InitializeNativeTargetAsmParser();
    });
}

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
    return sym->toPtr<float(*)(float)>()(x);
}
