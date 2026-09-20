// The embeddable compiler. See CompilerApi.h: source text in, diagnostics and
// object bytes out, no file touched.

#include "CompilerApi.h"

#include "AST.h"
#include "Codegen.h"
#include "Diagnostics.h"
#include "Lexer.h"
#include "ModuleLoader.h"
#include "parser.hpp"

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/Module.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Support/SmallVectorMemoryBuffer.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/TargetParser/Host.h>

#include <fstream>
#include <mutex>
#include <optional>
#include <regex>
#include <sstream>

namespace frust {
namespace {

void InitialiseLlvmOnce() {
    static std::once_flag once;
    std::call_once(once, [] {
        llvm::InitializeNativeTarget();
        llvm::InitializeNativeTargetAsmPrinter();
        llvm::InitializeNativeTargetAsmParser();
    });
}

void Add(CompileResult& result, Diagnostic::Phase phase, const std::string& file, int line, int column,
         std::string message, Diagnostic::Severity severity = Diagnostic::Severity::Error) {
    Diagnostic d;
    d.severity = severity;
    d.phase = phase;
    d.file = file;
    d.line = line;
    d.column = column;
    d.message = std::move(message);
    result.diagnostics.push_back(std::move(d));
}

// "frust: unterminated string literal at line 4" -> (message, line 4).
void AddFromLexerText(CompileResult& result, const std::string& file, const std::string& text) {
    static const std::regex atLine(R"(^(.*?)\s+at line (\d+)\s*$)");
    std::smatch m;
    if (std::regex_match(text, m, atLine))
        Add(result, Diagnostic::Phase::Lexer, file, std::stoi(m[2].str()), 0, m[1].str());
    else
        Add(result, Diagnostic::Phase::Lexer, file, 0, 0, text);
}

// Parses one source into `arena`. Returns null (with diagnostics) on any error.
Program* ParseOne(const SourceFile& source, AstArena& arena, CompileResult& result) {
    std::istringstream input(source.text);
    Lexer lexer(&input);
    Program* program = nullptr;
    std::vector<std::string> parseErrors;
    std::vector<ParseError> structured;
    Parser parser(lexer, arena, parseErrors, program, structured);
    parser.parse();

    bool failed = false;
    for (const auto& e : structured) {
        Add(result, Diagnostic::Phase::Parser, source.name, e.line, e.col, e.message);
        failed = true;
    }
    for (const auto& text : lexer.errors) {
        AddFromLexerText(result, source.name, text);
        failed = true;
    }
    if (!failed && !parseErrors.empty()) {
        // Errors the parser reported only as text.
        for (const auto& text : parseErrors) Add(result, Diagnostic::Phase::Parser, source.name, 0, 0, text);
        failed = true;
    }
    return (failed || !program) ? nullptr : program;
}

// The directory part of a source name, so `use self::x` looks next to the file that says it.
std::string DirectoryOf(const std::string& name) {
    const auto slash = name.find_last_of("/\\");
    return slash == std::string::npos ? std::string() : name.substr(0, slash + 1);
}

void OptimiseModule(llvm::Module& module) {
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
    MPM.run(module, MAM);
}

std::string IrText(const llvm::Module& module) {
    std::string text;
    llvm::raw_string_ostream out(text);
    module.print(out, nullptr);
    out.flush();
    return text;
}

} // namespace

std::string FormatDiagnostic(const Diagnostic& d) {
    std::ostringstream out;
    if (!d.file.empty()) out << d.file << ":";
    if (d.line > 0) {
        out << d.line << ":";
        if (d.column > 0) out << d.column << ":";
    }
    if (!d.file.empty() || d.line > 0) out << " ";
    out << (d.severity == Diagnostic::Severity::Error ? "error: " : "warning: ") << d.message;
    return out.str();
}

SourceProvider DiskSourceProvider() {
    return [](const std::string& path, std::string& text) {
        std::ifstream in(path, std::ios::binary);
        if (!in) return false;
        std::ostringstream all;
        all << in.rdbuf();
        text = all.str();
        return true;
    };
}

CompileResult Compile(const CompileRequest& request) {
    CompileResult result;
    InitialiseLlvmOnce();

    if (request.sources.empty()) {
        Add(result, Diagnostic::Phase::Parser, {}, 0, 0, "no source was given");
        return result;
    }

    AstArena arena;
    Program* merged = arena.NewProgram();

    // 1. Parse every source into the one arena and merge them, so a unit can
    //    span several files. Errors from all sources are collected before
    //    giving up, so one call reports everything that is wrong.
    bool parseFailed = false;
    for (const auto& source : request.sources) {
        Program* program = ParseOne(source, arena, result);
        if (!program) {
            parseFailed = true;
            continue;
        }

        // `use self::name;` - sibling files, from the host's provider.
        std::vector<std::string> moduleErrors;
        const std::string dir = DirectoryOf(source.name);
        SourceProvider siblings;
        if (request.siblingFiles) {
            siblings = [&request, dir](const std::string& name, std::string& text) {
                return request.siblingFiles(dir + name, text) || (!dir.empty() && request.siblingFiles(name, text));
            };
        }
        if (!ResolveSelfUsesWith(program, arena, siblings, moduleErrors))
            parseFailed = true;

        // `import pod, "version";` - pods, from the host's provider.
        for (auto* decl : program->decls) {
            if (decl->kind != DeclKind::Use || !decl->useDecl->isImport || decl->useDecl->pathSegments.empty()) continue;
            const std::string podName = decl->useDecl->pathSegments.front();
            const std::string version = decl->useDecl->importVersion;
            PodSource pod;
            if (!request.pods || !request.pods(podName, version, pod)) {
                moduleErrors.push_back("ModuleLoader: pod '" + podName + "' version " + version + " is not available to this compile");
                parseFailed = true;
                continue;
            }
            // The pod's entry is lib.fr; the other files are there for the
            // `use self::x;` lines lib.fr names.
            const SourceFile* entry = nullptr;
            for (const auto& podFile : pod.sources) {
                const auto slash = podFile.name.find_last_of("/\\");
                const std::string base = slash == std::string::npos ? podFile.name : podFile.name.substr(slash + 1);
                if (base == "lib.fr") { entry = &podFile; break; }
            }
            if (!entry) {
                moduleErrors.push_back("ModuleLoader: pod '" + podName + "' has no lib.fr");
                parseFailed = true;
                continue;
            }
            CompileResult podResult;
            Program* podProg = ParseOne(*entry, arena, podResult);
            for (auto& d : podResult.diagnostics) result.diagnostics.push_back(std::move(d));
            if (!podProg) { parseFailed = true; continue; }
            const SourceProvider podFiles = [&pod](const std::string& name, std::string& text) {
                for (const auto& f : pod.sources) {
                    const auto slash = f.name.find_last_of("/\\");
                    const std::string base = slash == std::string::npos ? f.name : f.name.substr(slash + 1);
                    if (base == name) { text = f.text; return true; }
                }
                return false;
            };
            std::vector<std::string> podModuleErrors;
            if (!ResolveSelfUsesWith(podProg, arena, podFiles, podModuleErrors)) {
                for (const auto& text : podModuleErrors) Add(result, Diagnostic::Phase::Modules, entry->name, 0, 0, text);
                parseFailed = true;
                continue;
            }
            MergeImportedPod(program, podProg, pod.ns.empty() ? podName : pod.ns);
        }

        for (const auto& text : moduleErrors) Add(result, Diagnostic::Phase::Modules, source.name, 0, 0, text);
        merged->decls.insert(merged->decls.end(), program->decls.begin(), program->decls.end());
    }
    if (parseFailed || result.hasErrors()) return result;

    // 2. Pod namespace prefix, exactly as the command-line compiler did.
    if (!request.podNamespace.empty()) {
        const std::string prefix = request.podNamespace + "::";
        for (auto* decl : merged->decls) {
            if (decl->kind == DeclKind::Function && decl->functionDecl && !decl->functionDecl->isExtern)
                decl->functionDecl->name = prefix + decl->functionDecl->name;
            else if (decl->kind == DeclKind::TypeAlias && decl->typeAliasDecl)
                decl->typeAliasDecl->name = prefix + decl->typeAliasDecl->name;
        }
    }

    // 3. Code generation. The compiler writes its messages to a diagnostic
    //    stream; capture that instead of the console and turn each line into
    //    a diagnostic.
    llvm::LLVMContext context;
    auto module = std::make_unique<llvm::Module>("FrustModule", context);

    std::ostringstream codegenText;
    bool codegenOk = false;
    {
        DiagnosticCapture capture(codegenText);
        Codegen codegen(context, *module);
        codegen.currentNamespace = request.podNamespace;
        codegenOk = codegen.compileProgram(*merged);
    }
    {
        std::istringstream lines(codegenText.str());
        std::string line;
        static const std::regex codegenPrefix(R"(^frust:\s*(?:codegen\s+)?(error|warning):?\s*)");
        while (std::getline(lines, line)) {
            if (line.empty()) continue;
            std::smatch m;
            Diagnostic::Severity severity = Diagnostic::Severity::Error;
            std::string message = line;
            if (std::regex_search(line, m, codegenPrefix)) {
                if (m[1].str() == "warning") severity = Diagnostic::Severity::Warning;
                message = line.substr(m.length(0));
            } else if (line.rfind("frust: ", 0) == 0) {
                message = line.substr(7);
            }
            Add(result, Diagnostic::Phase::Codegen, {}, 0, 0, message, severity);
        }
    }
    if (!codegenOk) {
        if (!result.hasErrors()) Add(result, Diagnostic::Phase::Codegen, {}, 0, 0, "code generation failed");
        return result;
    }

    // Check-only: parsing and code generation succeeded, which is what a
    // "does this compile" question needs. No point optimizing.
    if (!request.emitObject && !request.captureIr) {
        result.ok = !result.hasErrors();
        return result;
    }

    if (request.captureIr) result.irBeforeOptimization = IrText(*module);
    OptimiseModule(*module);
    if (request.captureIr) result.irAfterOptimization = IrText(*module);

    if (!request.emitObject) {
        result.ok = !result.hasErrors();
        return result;
    }

    // 4. Object code, into memory.
    const auto triple = llvm::sys::getDefaultTargetTriple();
    module->setTargetTriple(triple);

    std::string targetError;
    const auto* target = llvm::TargetRegistry::lookupTarget(triple, targetError);
    if (!target) {
        Add(result, Diagnostic::Phase::Backend, {}, 0, 0, targetError);
        return result;
    }

    llvm::TargetOptions options;
    std::unique_ptr<llvm::TargetMachine> machine(
        target->createTargetMachine(triple, "generic", "", options, std::optional<llvm::Reloc::Model>()));
    if (!machine) {
        Add(result, Diagnostic::Phase::Backend, {}, 0, 0, "could not create a target machine for " + triple);
        return result;
    }
    module->setDataLayout(machine->createDataLayout());

    llvm::SmallVector<char, 0> bytes;
    llvm::raw_svector_ostream stream(bytes);
    llvm::legacy::PassManager pass;
    if (machine->addPassesToEmitFile(pass, stream, nullptr, llvm::CodeGenFileType::ObjectFile)) {
        Add(result, Diagnostic::Phase::Backend, {}, 0, 0, "the target cannot emit an object file");
        return result;
    }
    pass.run(*module);

    result.object.assign(bytes.begin(), bytes.end());
    result.ok = !result.hasErrors() && !result.object.empty();
    return result;
}

} // namespace frust
