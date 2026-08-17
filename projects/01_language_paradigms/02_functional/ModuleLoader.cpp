#include "ModuleLoader.h"
#include "Lexer.h"
#include "parser.hpp"
#include <frate/FrateResolver.h>
#include <frate/FrateCache.h>
#include <frate/FrateConfig.h>

#include <sstream>
#include <fstream>
#include <iostream>

namespace frust {

static Program* ParseSource(std::istream& input, AstArena& arena, std::vector<std::string>& parseErrors) {
    Lexer lexer(&input);
    Program* result = nullptr;
    Parser parser(lexer, arena, parseErrors, result);
    parser.parse();
    parseErrors.insert(parseErrors.end(), lexer.errors.begin(), lexer.errors.end());
    return result;
}

bool ResolveImports(Program* prog, AstArena& arena, std::vector<std::string>& errors) {
    if (!prog) return false;

    frate::FrateCache cache;
    frate::FrateConfig config;
    config.load(juce::File::getCurrentWorkingDirectory().getChildFile("frate.json"));
    bool success = true;

    // Collect pods to import. `use self::X;` is a build-inclusion marker
    // for frate, not an import - the referenced file's declarations
    // arrive in this same Program some other way (frate passes it as a
    // separate frust_compiler argument), so it's a deliberate no-op here.
    std::vector<std::string> podsToImport;
    for (auto* decl : prog->decls) {
        if (decl->kind == DeclKind::Use && !decl->useDecl->isSelfUse) {
            if (!decl->useDecl->pathSegments.empty()) {
                podsToImport.push_back(decl->useDecl->pathSegments.front());
            }
        }
    }

    for (const auto& podName : podsToImport) {
        std::string version = "";
        for (const auto& dep : config.getDependencies()) {
            if (dep.name == podName) {
                version = dep.version;
                break;
            }
        }

        if (version.empty()) {
            errors.push_back("ModuleLoader: Pod '" + podName + "' is not listed in frate.json.");
            success = false;
            continue;
        }

        if (!cache.isCached(podName, version)) {
            errors.push_back("ModuleLoader: Pod '" + podName + "' v" + version + " is not cached. Run 'frate install' first.");
            success = false;
            continue;
        }

        std::string libPath = cache.getCachedPodDir(podName, version).getChildFile("src").getChildFile("lib.fr").getFullPathName().toStdString();
        std::ifstream file(libPath);
        if (!file.is_open()) {
            errors.push_back("ModuleLoader: Pod '" + podName + "' missing src/lib.fr");
            success = false;
            continue;
        }

        Program* podProg = ParseSource(file, arena, errors);
        if (!podProg || !errors.empty()) {
            errors.push_back("ModuleLoader: Failed to parse lib.fr for pod '" + podName + "'");
            success = false;
            continue;
        }

        // Prefix all declarations in the pod with the podName
        for (auto* decl : podProg->decls) {
            std::string prefix = podName + "::";
            if (decl->kind == DeclKind::Function && decl->functionDecl) {
                decl->functionDecl->name = prefix + decl->functionDecl->name;
                decl->functionDecl->isExtern = true; // Skip LLVM codegen for dependency functions
            } else if (decl->kind == DeclKind::Struct && decl->structDecl) {
                decl->structDecl->name = prefix + decl->structDecl->name;
            } else if (decl->kind == DeclKind::TypeAlias && decl->typeAliasDecl) {
                decl->typeAliasDecl->name = prefix + decl->typeAliasDecl->name;
            } else if (decl->kind == DeclKind::Effect && decl->effectDecl) {
                decl->effectDecl->name = prefix + decl->effectDecl->name;
            } else if (decl->kind == DeclKind::Component && decl->componentDecl) {
                decl->componentDecl->name = prefix + decl->componentDecl->name;
            }
            prog->decls.push_back(decl);
        }
    }

    return success;
}

} // namespace frust
