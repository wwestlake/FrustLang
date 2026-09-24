// The disk half of FRust's module loading: sibling files, pods and the Frate cache read from real
// folders. This is what the command-line tools and the older file-based hosts use; it is NOT part of
// frust_lang. A host that keeps its sources somewhere else (the Suite's VFS) never links this file:
// it gives the compiler an environment instead (HostEnvironment.h).

#include "DiskLoader.h"
#include "ModuleLoader.h"

#include "Lexer.h"
#include "parser.hpp"

#include <frate/FrateResolver.h>
#include <frate/FrateCache.h>
#include <frate/FrateConfig.h>

#include <fstream>
#include <iostream>
#include <sstream>

namespace frust {

bool ResolveSelfUsesFromDisk(Program* prog, AstArena& arena, const std::string& baseDir, std::vector<std::string>& errors) {
    // The command-line compiler and the plugin host's file loader: sibling
    // files sit next to the importing file on disk.
    const SourceProvider disk = [&baseDir](const std::string& name, std::string& text) {
        juce::File candidate = juce::File(baseDir).getChildFile(juce::String(name));
        if (!candidate.existsAsFile()) return false;
        std::ifstream in(candidate.getFullPathName().toStdString());
        if (!in.is_open()) return false;
        std::ostringstream all;
        all << in.rdbuf();
        text = all.str();
        return true;
    };
    return ResolveSelfUsesWith(prog, arena, disk, errors);
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

// Pods from frate.json and the Frate cache in the working directory - the
// disk-backed PodProvider the command-line compiler passes to Compile().
PodProvider FratePodProvider() {
    return [](const std::string& name, const std::string&, PodSource& pod) {
        frate::FrateCache cache;
        frate::FrateConfig config;
        config.load(juce::File::getCurrentWorkingDirectory().getChildFile("frate.json"));
        std::string version;
        for (const auto& dep : config.getDependencies())
            if (dep.name == name) { version = dep.version; break; }
        if (version.empty() || !cache.isCached(name, version)) return false;

        const juce::File podDir = cache.getCachedPodDir(name, version);
        const auto read = DiskSourceProvider();
        juce::Array<juce::File> files;
        podDir.getChildFile("src").findChildFiles(files, juce::File::findFiles, false, "*.fr;*.frust");
        for (const auto& f : files) {
            SourceFile file;
            file.name = f.getFileName().toStdString();
            if (!read(f.getFullPathName().toStdString(), file.text)) return false;
            pod.sources.push_back(std::move(file));
        }
        frate::FrateConfig podConfig;
        podConfig.load(podDir.getChildFile("frate.json"));
        pod.ns = podConfig.getMetadata().namespacePath;
        return true;
    };
}

bool ResolveImportsFromDisk(Program* prog, AstArena& arena, std::vector<std::string>& errors) {
    if (!prog) return false;

    frate::FrateCache cache;
    frate::FrateConfig config;
    config.load(juce::File::getCurrentWorkingDirectory().getChildFile("frate.json"));
    bool success = true;

    // Collect explicit-version imports. `use self::X;` is a sibling-file
    // build-inclusion marker, and bare `use pod;` is Frate's normal
    // dependency import shorthand (version comes from frate.json, workspace
    // members may be local). Both are handled before frust_compiler is
    // launched by Frate, so this loader only owns the low-level
    // `import pod, "version";` escape hatch.
    std::vector<std::string> podsToImport;
    for (auto* decl : prog->decls) {
        if (decl->kind == DeclKind::Use) {
            if (decl->useDecl->isImport) {
                if (!decl->useDecl->pathSegments.empty()) {
                    podsToImport.push_back(decl->useDecl->pathSegments.front());
                }
            } else if (!decl->useDecl->isSelfUse && decl->useDecl->pathSegments.size() == 1) {
                std::string name = decl->useDecl->pathSegments.front();
                if (name != "self") {
                    for (const auto& dep : config.getDependencies()) {
                        if (dep.name == name) {
                            podsToImport.push_back(name);
                            break;
                        }
                    }
                }
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
        Program* podProg = ParseProgram(file, arena, errors);
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
        if (!ResolveSelfUsesFromDisk(podProg, arena, cachedPodDir.getChildFile("src").getFullPathName().toStdString(), errors)) {
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

        MergeImportedPod(prog, podProg, qualifiedName);
    }

    return success;
}


void InstallDiskResolvers() {
    SetImportResolver(ResolveImportsFromDisk);
    SetFileReader([](const std::string& path, std::string& text) {
        std::ifstream in(path, std::ios::binary);
        if (!in) return false;
        std::ostringstream all;
        all << in.rdbuf();
        text = all.str();
        return true;
    });
}

} // namespace frust
