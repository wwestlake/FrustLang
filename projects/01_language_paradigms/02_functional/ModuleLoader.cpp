#include "ModuleLoader.h"
#include "Lexer.h"
#include "parser.hpp"

#include <mutex>
#include <sstream>

namespace frust {

Program* ParseProgram(std::istream& input, AstArena& arena, std::vector<std::string>& parseErrors) {
    Lexer lexer(&input);
    Program* result = nullptr;
    std::vector<ParseError> structuredErrors; // unused here - frust_lsp has its own ParseSource that reads these
    Parser parser(lexer, arena, parseErrors, result, structuredErrors);
    parser.parse();
    parseErrors.insert(parseErrors.end(), lexer.errors.begin(), lexer.errors.end());
    return result;
}

// Core of self-use resolution, independent of where the files live: `files`
// is asked for "X.frust" and then "X.fr". The disk overload below and the
// embeddable compiler (CompilerApi.cpp) both use it.
bool ResolveSelfUsesWith(Program* prog, AstArena& arena, const SourceProvider& files, std::vector<std::string>& errors) {
    if (!prog) return false;
    bool success = true;

    // Snapshot first - the loop body appends to prog->decls, which
    // would invalidate a live iterator over the same vector.
    std::vector<Decl*> selfUseDecls;
    for (auto* d : prog->decls) {
        if (d->kind == DeclKind::Use && d->useDecl->isSelfUse) selfUseDecls.push_back(d);
    }

    for (auto* selfDecl : selfUseDecls) {
        if (selfDecl->useDecl->pathSegments.empty()) continue;
        std::string modName = selfDecl->useDecl->pathSegments.front();

        std::string modPath = modName + ".frust";
        std::string text;
        if (!files || !files(modPath, text)) {
            modPath = modName + ".fr";
            if (!files || !files(modPath, text)) {
                errors.push_back("ModuleLoader: use self::" + modName + " names a missing file (tried " + modName + ".frust then " + modName + ".fr next to the importing file)");
                success = false;
                continue;
            }
        }

        // Same "errors is shared/accumulated across every self-use this
        // call processes" reasoning as ResolveImports' own pod loop -
        // compare the count before/after THIS file, not whether the
        // shared vector is empty overall.
        size_t errorsBefore = errors.size();
        std::istringstream modFile(text);
        Program* modProg = ParseProgram(modFile, arena, errors);
        if (!modProg || errors.size() > errorsBefore) {
            errors.push_back("ModuleLoader: failed to parse '" + modPath + "' for use self::" + modName);
            success = false;
            continue;
        }
        prog->decls.insert(prog->decls.end(), modProg->decls.begin(), modProg->decls.end());
    }

    return success;
}

// Brings an imported pod's declarations into `prog` under `qualifiedName`.
// Shared by the on-disk import path below and the embeddable compiler.
void MergeImportedPod(Program* prog, Program* podProg, const std::string& qualifiedName) {
    // Prefix all declarations in the pod with its qualified name
    for (auto* decl : podProg->decls) {
        std::string prefix = qualifiedName + "::";
        if (decl->kind == DeclKind::Function && decl->functionDecl) {
            decl->functionDecl->name = prefix + decl->functionDecl->name;
            decl->functionDecl->isExtern = true; // Skip LLVM codegen for dependency functions
        } else if (decl->kind == DeclKind::Struct && decl->structDecl) {
        } else if (decl->kind == DeclKind::TypeAlias && decl->typeAliasDecl) {
            decl->typeAliasDecl->name = prefix + decl->typeAliasDecl->name;
        } else if (decl->kind == DeclKind::Effect && decl->effectDecl) {
            decl->effectDecl->name = prefix + decl->effectDecl->name;
        } else if (decl->kind == DeclKind::Component && decl->componentDecl) {
            decl->componentDecl->name = prefix + decl->componentDecl->name;
        } else if (decl->kind == DeclKind::Impl && decl->implDecl) {
            for (auto* method : decl->implDecl->methods) {
                method->isExtern = true;
            }
        }

        prog->decls.push_back(decl);
    }
}

namespace {
std::mutex g_hookMutex;
ImportResolver g_importResolver;
FileReader g_fileReader;
} // namespace

void SetImportResolver(ImportResolver resolver) {
    std::lock_guard<std::mutex> lock(g_hookMutex);
    g_importResolver = std::move(resolver);
}

void SetFileReader(FileReader reader) {
    std::lock_guard<std::mutex> lock(g_hookMutex);
    g_fileReader = std::move(reader);
}

bool ReadFileThroughHost(const std::string& path, std::string& text) {
    FileReader reader;
    {
        std::lock_guard<std::mutex> lock(g_hookMutex);
        reader = g_fileReader;
    }
    return reader && reader(path, text);
}

bool ResolveImports(Program* prog, AstArena& arena, std::vector<std::string>& errors) {
    if (!prog) return false;
    ImportResolver resolver;
    {
        std::lock_guard<std::mutex> lock(g_hookMutex);
        resolver = g_importResolver;
    }
    // No resolver installed: the host resolves imports itself (or has none), so there is nothing to do here.
    return resolver ? resolver(prog, arena, errors) : true;
}

} // namespace frust
