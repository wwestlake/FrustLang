#include "ReplSession.h"

#include "AST.h"
#include "Codegen.h"
#include "Lexer.h"
#include "ModuleLoader.h"
#include "parser.hpp"

#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/ExecutionEngine/Orc/ExecutionUtils.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Support/Error.h>
#include <llvm/Support/JSON.h>
#include <llvm/Support/TargetSelect.h>

#include <map>
#include <sstream>

namespace frust {

namespace {
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

        llvm::ModulePassManager MPM = PB.buildPerModuleDefaultPipeline(llvm::OptimizationLevel::O2);
        MPM.run(M, MAM);
    }
}

struct ReplSession::Impl {
    // Name -> last computed value. Everything's f64 here (see
    // compileAnonymous()'s "always returns f64" choice) - not a real type
    // system, just enough to make a numeric REPL context persist and
    // round-trip through JSON.
    std::map<std::string, double> boundValues;

    Impl() {
        // LLVM's target init is process-global and idempotent, but calling
        // it repeatedly is wasted work - once per session is enough since
        // a session outlives every evaluate() call.
        llvm::InitializeNativeTarget();
        llvm::InitializeNativeTargetAsmPrinter();
        llvm::InitializeNativeTargetAsmParser();
    }
};

ReplSession::ReplSession() : impl(std::make_unique<Impl>()) {}
ReplSession::~ReplSession() = default;

std::string ReplSession::evaluateStmt(Expr* stmtExpr) {
    auto context = std::make_unique<llvm::LLVMContext>();
    auto module = std::make_unique<llvm::Module>("FrustReplModule", *context);
    Codegen codegen(*context, *module);

    // Replay every previously-bound variable into this otherwise-fresh
    // compile, so e.g. `let x = 5` on one line/statement and `x + 1` on the
    // next actually resolves `x`.
    for (auto& [name, value] : impl->boundValues) codegen.seedConstant(name, value);

    if (!codegen.compileAnonymous("__repl_eval", stmtExpr)) return "(codegen error)";

    auto jitOrErr = llvm::orc::LLJITBuilder().create();
    if (!jitOrErr) { llvm::consumeError(jitOrErr.takeError()); return "(JIT init failed)"; }
    auto jit = std::move(*jitOrErr);

    optimizeModule(*module);

    llvm::orc::ThreadSafeModule tsm(std::move(module), std::move(context));
    if (auto err = jit->addIRModule(std::move(tsm))) { llvm::consumeError(std::move(err)); return "(JIT load failed)"; }

    auto sym = jit->lookup("__repl_eval");
    if (!sym) { llvm::consumeError(sym.takeError()); return "(JIT lookup failed)"; }

    double result = sym->toPtr<double (*)()>()();

    if (stmtExpr->kind == ExprKind::Let) impl->boundValues[stmtExpr->text] = result;

    std::ostringstream out;
    out << result;
    return out.str();
}

std::string ReplSession::evaluate(const std::string& line) {
    std::istringstream input(line);
    AstArena arena;
    std::vector<std::string> parseErrors;

    Lexer lexer(&input);
    Program* prog = nullptr;
    std::vector<ParseError> structuredErrors; // unused here - frust_lsp has its own ParseSource that reads these
    Parser parser(lexer, arena, parseErrors, prog, structuredErrors);
    parser.parse();

    parseErrors.insert(parseErrors.end(), lexer.errors.begin(), lexer.errors.end());

    if (prog) {
        ResolveImports(prog, arena, parseErrors);
    }

    if (!parseErrors.empty() || !prog) {
        if (parseErrors.empty()) return "(parse error)";
        
        std::string errs;
        for (size_t i = 0; i < parseErrors.size(); ++i) {
            if (i > 0) errs += "\n";
            errs += parseErrors[i];
        }
        return errs;
    }
    
    if (prog->decls.empty()) return "";

    auto* last = prog->decls.back();
    if (last->kind != DeclKind::TopLevelStmt) {
        // A function/struct/type/effect/component decl was typed - it
        // codegens fine but can't be called from a later, separate
        // evaluate() call yet (only bound *values*, not function defs,
        // persist across calls right now).
        return "(defined)";
    }

    return evaluateStmt(last->topLevelStmt);
}

std::vector<std::string> ReplSession::runScript(const std::string& source) {
    std::istringstream input(source);
    AstArena arena;
    std::vector<std::string> parseErrors;

    Lexer lexer(&input);
    Program* prog = nullptr;
    std::vector<ParseError> structuredErrors; // unused here - frust_lsp has its own ParseSource that reads these
    Parser parser(lexer, arena, parseErrors, prog, structuredErrors);
    parser.parse();

    parseErrors.insert(parseErrors.end(), lexer.errors.begin(), lexer.errors.end());

    if (prog) {
        ResolveImports(prog, arena, parseErrors);
    }

    if (!parseErrors.empty() || !prog) {
        if (parseErrors.empty()) return { "(parse error)" };
        
        std::string errs;
        for (size_t i = 0; i < parseErrors.size(); ++i) {
            if (i > 0) errs += "\n";
            errs += parseErrors[i];
        }
        return { errs };
    }

    auto context = std::make_unique<llvm::LLVMContext>();
    auto module = std::make_unique<llvm::Module>("FrustScriptModule", *context);
    Codegen codegen(*context, *module);

    for (auto& [name, value] : impl->boundValues) codegen.seedConstant(name, value);

    if (!codegen.compileProgram(*prog)) {
        return { "(codegen error in declarations)" };
    }

    auto jitOrErr = llvm::orc::LLJITBuilder().create();
    if (!jitOrErr) return { "(JIT init failed)" };
    auto jit = std::move(*jitOrErr);

    auto generator = llvm::orc::DynamicLibrarySearchGenerator::GetForCurrentProcess(
        jit->getDataLayout().getGlobalPrefix());
    if (generator) {
        jit->getMainJITDylib().addGenerator(std::move(*generator));
    }

    std::vector<std::string> results;
    results.reserve(prog->decls.size());
    std::vector<std::string> stmtNames;
    int stmtIdx = 0;

    for (auto* decl : prog->decls) {
        if (decl->kind == DeclKind::TopLevelStmt) {
            std::string name = "__repl_stmt_" + std::to_string(stmtIdx++);
            stmtNames.push_back(name);
            if (!codegen.compileAnonymous(name, decl->topLevelStmt)) {
                return { "(codegen error in statement)" };
            }
        }
    }

    optimizeModule(*module);

    llvm::orc::ThreadSafeModule tsm(std::move(module), std::move(context));
    if (auto err = jit->addIRModule(std::move(tsm))) return { "(JIT load failed)" };

    stmtIdx = 0;
    for (auto* decl : prog->decls) {
        if (decl->kind == DeclKind::TopLevelStmt) {
            auto sym = jit->lookup(stmtNames[stmtIdx++]);
            if (!sym) {
                results.push_back("(JIT lookup failed)");
                continue;
            }
            double result = sym->toPtr<double (*)()>()();
            if (decl->topLevelStmt->kind == ExprKind::Let) {
                impl->boundValues[decl->topLevelStmt->text] = result;
            }
            std::ostringstream out;
            out << result;
            results.push_back(out.str());
        } else {
            results.push_back("(defined)");
        }
    }
    return results;
}

void ReplSession::reset() {
    impl->boundValues.clear();
}

std::string ReplSession::exportAsJson() const {
    llvm::json::Object obj;
    for (auto& [name, value] : impl->boundValues) obj[name] = value;

    std::string text;
    llvm::raw_string_ostream os(text);
    os << llvm::json::Value(std::move(obj));
    return text;
}

bool ReplSession::importFromJson(const std::string& json) {
    auto parsed = llvm::json::parse(json);
    if (!parsed) { llvm::consumeError(parsed.takeError()); return false; }

    auto* obj = parsed->getAsObject();
    if (!obj) return false;

    std::map<std::string, double> newValues;
    for (auto& kv : *obj) {
        auto num = kv.second.getAsNumber();
        if (!num) return false;
        newValues[kv.first.str()] = *num;
    }

    impl->boundValues = std::move(newValues);
    return true;
}

std::vector<ReplSession::Binding> ReplSession::listBindings() const {
    std::vector<Binding> result;
    result.reserve(impl->boundValues.size());
    for (auto& [name, value] : impl->boundValues) result.push_back({name, "f64", value});
    return result;
}

} // namespace frust
