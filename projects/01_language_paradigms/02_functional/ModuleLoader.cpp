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
    std::vector<ParseError> structuredErrors; // unused here - frust_lsp has its own ParseSource that reads these
    Parser parser(lexer, arena, parseErrors, result, structuredErrors);
    parser.parse();
    parseErrors.insert(parseErrors.end(), lexer.errors.begin(), lexer.errors.end());
    return result;
}

bool ResolveSelfUses(Program* prog, AstArena& arena, const std::string& baseDir, std::vector<std::string>& errors) {
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

        juce::File base(baseDir);
        juce::File candidate = base.getChildFile(juce::String(modName) + ".frust");
        if (!candidate.existsAsFile()) {
            candidate = base.getChildFile(juce::String(modName) + ".fr");
        }
        std::string modPath = candidate.getFullPathName().toStdString();
        std::ifstream modFile(modPath);
        if (!modFile.is_open()) {
            errors.push_back("ModuleLoader: use self::" + modName + " names a missing file (tried .frust then .fr next to the importing file): " + modPath);
            success = false;
            continue;
        }

        // Same "errors is shared/accumulated across every self-use this
        // call processes" reasoning as ResolveImports' own pod loop -
        // compare the count before/after THIS file, not whether the
        // shared vector is empty overall.
        size_t errorsBefore = errors.size();
        Program* modProg = ParseSource(modFile, arena, errors);
        if (!modProg || errors.size() > errorsBefore) {
            errors.push_back("ModuleLoader: failed to parse '" + modPath + "' for use self::" + modName);
            success = false;
            continue;
        }
        prog->decls.insert(prog->decls.end(), modProg->decls.begin(), modProg->decls.end());
    }

    return success;
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

        juce::File cachedPodDir = cache.getCachedPodDir(podName, version);
        std::string libPath = cachedPodDir.getChildFile("src").getChildFile("lib.fr").getFullPathName().toStdString();
        std::ifstream file(libPath);
        if (!file.is_open()) {
            errors.push_back("ModuleLoader: Pod '" + podName + "' missing src/lib.fr");
            success = false;
            continue;
        }

        // errors is the SHARED, ACCUMULATED vector across every pod this
        // whole ResolveImports call processes - checking !errors.empty()
        // here was checking whether ANY pod ever failed, not whether THIS
        // one did. One early pod's failure permanently poisoned every
        // pod after it in the same loop, even ones that parsed cleanly.
        // Compare the count before/after this specific call instead.
        size_t errorsBefore = errors.size();
        Program* podProg = ParseSource(file, arena, errors);
        if (!podProg || errors.size() > errorsBefore) {
            errors.push_back("ModuleLoader: Failed to parse lib.fr for pod '" + podName + "'");
            success = false;
            continue;
        }

        // lib.fr's own `use self::X;` lines name sibling files (frate's
        // explicit build-inclusion mechanism) that frate would pass to
        // frust_compiler directly as separate arguments for an in-pod
        // build - cross-pod import has no such argument list, so this
        // has to walk the same self-use decls itself and pull each
        // file's declarations in, or a multi-file pod's actual content
        // (almost all of it, for a pod like core where lib.fr is just
        // the self-use list) would silently never get imported at all.
        // Shared with frust_plugin_host's own multi-file support
        // (LANGUAGE_GAPS.md #8) - same resolution logic, just a
        // different base directory.
        if (!ResolveSelfUses(podProg, arena, cachedPodDir.getChildFile("src").getFullPathName().toStdString(), errors)) {
            success = false;
        }

        // A pod's declared frate.json `namespace` (e.g. "frust::core")
        // takes over qualification instead of the bare pod name, if
        // present - lets stdlib pods share the reserved `frust` root
        // without every pod needing one.
        frate::FrateConfig podConfig;
        podConfig.load(cachedPodDir.getChildFile("frate.json"));
        const std::string& declaredNamespace = podConfig.getMetadata().namespacePath;
        std::string qualifiedName = declaredNamespace.empty() ? podName : declaredNamespace;

        // Prefix all declarations in the pod with its qualified name
        for (auto* decl : podProg->decls) {
            std::string prefix = qualifiedName + "::";
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
