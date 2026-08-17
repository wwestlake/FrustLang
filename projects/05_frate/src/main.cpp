#include <iostream>
#include <string>
#include <vector>

#include <juce_core/juce_core.h>

#include <frate/FratePodBuilder.h>
#include <frate/FrateCache.h>
#include <frate/FrateRegistryClient.h>
#include <frate/FrateResolver.h>
#include <frate/FrateConfig.h>
#include "PodMetadataJson.h"
#include <map>

// The CLI has no login flow of its own - it reads the token the IDE's
// DesktopAuthSession already persisted from its OAuth sign-in, so signing
// in once via the IDE lets both the Producer panel and `frate publish` work.
// See FRATE_SPEC.md section 7.5.
juce::String loadIdeAuthToken() {
    juce::File sessionFile = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                                  .getChildFile("LagDaemonResearchIDE")
                                  .getChildFile("desktop-auth.json");
    if (!sessionFile.existsAsFile()) return {};

    auto xml = juce::parseXML(sessionFile);
    if (!xml) return {};

    juce::String token = xml->getStringAttribute("token");
    juce::int64 expiresAt = xml->getStringAttribute("expiresAt").getLargeIntValue();
    if (token.isEmpty() || expiresAt <= juce::Time::getCurrentTime().toMilliseconds()) return {};

    return token;
}

bool linkExecutable(const std::vector<juce::String>& objFiles, const juce::File& finalBin) {
    juce::ChildProcess linker;

    // Check if clang is in path
    juce::ChildProcess checkClang;
    bool clangFound = false;
    if (checkClang.start(juce::StringArray("clang", "--version"))) {
        checkClang.waitForProcessToFinish(2000);
        if (checkClang.getExitCode() == 0) {
            clangFound = true;
        }
    }
    
    if (clangFound) {
        juce::StringArray linkerArgs;
        linkerArgs.add("clang");
        for (auto& obj : objFiles) linkerArgs.add(obj);
        linkerArgs.add("-o");
        linkerArgs.add(finalBin.getFullPathName());
        
        if (linker.start(linkerArgs)) {
            juce::String linkerOut = linker.readAllProcessOutput();
            if (linker.getExitCode() != 0) {
                std::cerr << "Linker failed:\n" << linkerOut << "\n";
                return false;
            }
            return true;
        }
    } else {
        // Try MSVC link.exe via VsDevCmd
#if JUCE_WINDOWS
        juce::String vsWherePath = "C:\\Program Files (x86)\\Microsoft Visual Studio\\Installer\\vswhere.exe";
        if (juce::File(vsWherePath).existsAsFile()) {
            juce::ChildProcess vswhere;
            juce::String vsWhereCmd = "\"" + vsWherePath + "\" -latest -property installationPath";
            if (vswhere.start(vsWhereCmd)) {
                juce::String vsPath = vswhere.readAllProcessOutput().trim();
                if (vsPath.isNotEmpty()) {
                    juce::String vsDevCmd = vsPath + "\\Common7\\Tools\\VsDevCmd.bat";
                    if (juce::File(vsDevCmd).existsAsFile()) {
                        juce::String linkCmd = "call \"" + vsDevCmd + "\" -arch=x64 && link.exe /OUT:\"" + finalBin.getFullPathName() + "\" /ENTRY:main";
                        for (auto& obj : objFiles) linkCmd += " \"" + obj + "\"";
                        
                        juce::String fullCmd = "cmd.exe /c \"" + linkCmd + "\"";
                        if (linker.start(fullCmd)) {
                            juce::String linkerOut = linker.readAllProcessOutput();
                            if (linker.getExitCode() != 0) {
                                std::cerr << "Linker failed:\n" << linkerOut << "\n";
                                return false;
                            }
                            return true;
                        } else {
                            std::cerr << "Error: Failed to launch MSVC linker process.\n";
                        }
                    }
                }
            } else {
                std::cerr << "Error: Failed to launch vswhere.exe for MSVC discovery.\n";
            }
        }
#endif
    }
    
    std::cerr << "Error: Failed to find clang or MSVC linker in PATH.\n";
    return false;
}

// A pod's source files are exactly its entry file plus every sibling file
// named by a `use self::X;` line in that entry file, in the order those
// lines appear - explicit, developer-controlled, no directory scanning.
// A pod with no such lines just compiles its single entry file.
static bool collectSelfUseFiles(const juce::File& entryFile, juce::Array<juce::File>& sourceFiles, juce::String& errorOut) {
    sourceFiles.add(entryFile);

    juce::StringArray lines = juce::StringArray::fromLines(entryFile.loadFileAsString());
    for (const auto& rawLine : lines) {
        // Strip a trailing `//` comment before matching - directives are
        // commonly annotated inline (`use self::math; // trig, abs, ...`).
        juce::String line = rawLine.upToFirstOccurrenceOf("//", false, false).trim();
        if (!line.startsWith("use self::")) continue;

        if (!line.endsWith(";")) {
            errorOut = "Malformed 'use self::' directive (missing ';'): " + rawLine.trim();
            return false;
        }

        juce::String moduleName = line.substring(juce::String("use self::").length(), line.length() - 1).trim();
        if (moduleName.isEmpty() || moduleName.containsAnyOf(" \t:")) {
            errorOut = "Malformed 'use self::' directive: " + rawLine.trim();
            return false;
        }

        juce::File modFile = entryFile.getParentDirectory().getChildFile(moduleName + ".fr");
        if (!modFile.existsAsFile()) {
            errorOut = "use self::" + moduleName + " - file not found: " + modFile.getFullPathName();
            return false;
        }
        sourceFiles.add(modFile);
    }
    return true;
}

bool buildPod(const juce::File& podDir, bool isRun, const std::map<std::string, juce::File>& localWorkspaceMap, std::vector<juce::String>& workspaceObjFiles) {
    juce::File frateJson = podDir.getChildFile("frate.json");
    if (!frateJson.existsAsFile()) {
        std::cerr << "Error: No frate.json found in " << podDir.getFullPathName() << ".\n";
        return false;
    }
    
    frate::FrateConfig config;
    config.load(frateJson);
    const auto& meta = config.getMetadata();
    
    juce::String entryPoint = (meta.type == "lib") ? "src/lib.fr" : "src/main.fr";
    juce::File entryFile = podDir.getChildFile(entryPoint);
    
    if (!entryFile.existsAsFile()) {
        std::cerr << "Error: Entry point " << entryPoint << " not found in " << podDir.getFullPathName() << ".\n";
        return false;
    }

    if (isRun && meta.type == "lib") {
        std::cerr << "Error: Cannot run a library pod.\n";
        return false;
    }
    
    std::cout << "Compiling dependencies for " << meta.name << "...\n";
    frate::FrateCache cache;
    std::vector<juce::String> objFiles;
    
    for (const auto& dep : config.getDependencies()) {
        juce::File depDir;
        if (localWorkspaceMap.count(dep.name) > 0) {
            depDir = localWorkspaceMap.at(dep.name);
        } else {
            if (!cache.isCached(dep.name, dep.version)) {
                std::cerr << "Error: Dependency " << dep.name << " v" << dep.version << " is not cached. Run 'frate install' first.\n";
                return false;
            }
            depDir = cache.getCachedPodDir(dep.name, dep.version);
        }
        
        juce::File depSrc = depDir.getChildFile("src").getChildFile("lib.fr");
        // For local workspace members, we look in their build directory for the object file
        juce::File depObj;
        if (localWorkspaceMap.count(dep.name) > 0) {
            depObj = depDir.getChildFile("build").getChildFile(juce::String(dep.name) + ".o");
        } else {
            depObj = depDir.getChildFile(juce::String(dep.name) + ".o");
        }
        
        if (!depObj.existsAsFile()) {
            std::cerr << "Error: Dependency object file not found: " << depObj.getFullPathName() << "\n";
            return false;
        }
        
        objFiles.push_back(depObj.getFullPathName());
        // For workspace members, add this object file so the workspace can collect them if needed
        workspaceObjFiles.push_back(depObj.getFullPathName());
    }
    
    // A pod's source isn't limited to its single entry file - sibling
    // files (math.fr, console_io.fr, ...) get compiled together as one
    // unit via frust_compiler's multi-file support (--emit-obj <output>
    // <input...>), but which files those are is controlled entirely by
    // `use self::X;` lines in the entry file - not a directory scan. A
    // pod with files sitting in src/ that nothing `use self::`s just
    // doesn't compile them; that's the point (explicit, not assumed).
    juce::Array<juce::File> sourceFiles;
    juce::String collectError;
    if (!collectSelfUseFiles(entryFile, sourceFiles, collectError)) {
        std::cerr << "Error: " << collectError << "\n";
        return false;
    }

    juce::File buildDir = podDir.getChildFile("build");
    buildDir.createDirectory();

    juce::File mainObj = buildDir.getChildFile(juce::String(meta.name) + ".o");

    std::cout << "Compiling " << meta.name << " (" << sourceFiles.size() << " source file(s))...\n";
    juce::ChildProcess compiler;
    juce::StringArray args;
    args.add("frust_compiler");
    args.add("--emit-obj");
    args.add(mainObj.getFullPathName());
    for (const auto& f : sourceFiles) args.add(f.getFullPathName());

    if (compiler.start(args)) {
        juce::String output = compiler.readAllProcessOutput();
        if (compiler.getExitCode() != 0) {
            std::cerr << "Compiler exited with code " << compiler.getExitCode() << "\n" << output << "\n";
            return false;
        }
    } else {
        std::cerr << "Error: Failed to launch frust_compiler. Ensure it is in your PATH.\n";
        return false;
    }
    
    objFiles.push_back(mainObj.getFullPathName());
    workspaceObjFiles.push_back(mainObj.getFullPathName());
    
    juce::String outExt = (meta.type == "lib") ? ".lib" : ".exe";
    juce::File finalBin = buildDir.getChildFile(juce::String(meta.name) + outExt);
    
    if (meta.type == "bin") {
        std::cout << "Linking executable...\n";
        if (!linkExecutable(objFiles, finalBin)) {
            return false;
        }
        
        if (isRun) {
            std::cout << "Running " << finalBin.getFileName() << "...\n";
            juce::ChildProcess runner;
            juce::StringArray runArgs;
            runArgs.add(finalBin.getFullPathName());
            if (runner.start(runArgs)) {
                std::cout << runner.readAllProcessOutput();
                int exitCode = runner.getExitCode();
                if (exitCode != 0) std::cerr << "Process exited with code " << exitCode << "\n";
                return exitCode == 0;
            } else {
                std::cerr << "Failed to execute " << finalBin.getFullPathName() << "\n";
                return false;
            }
        }
    } else {
        std::cout << "Library " << meta.name << " built successfully (object file at " << mainObj.getFullPathName() << ").\n";
    }
    return true;
}

void printUsage() {
    std::cout << "frate - Frust Package Manager\n\n";
    std::cout << "Usage:\n";
    std::cout << "  frate new <pod_name> [--lib]     Scaffold a new pod (defaults to executable)\n";
    std::cout << "  frate build                      Compile the current pod\n";
    std::cout << "  frate run                        Compile and run the current executable pod\n";
    std::cout << "  frate package                    Package the current directory into a .frpod\n";
    std::cout << "  frate install                    Install the packaged .frpod to local cache\n";
    std::cout << "  frate update                     Fetch remote dependencies into local cache\n";
    std::cout << "  frate add <pod_name> <version>   Add a dependency to frate.json in current dir\n";
    std::cout << "  frate publish [license]          Publish the packaged .frpod to registry (default license: MIT)\n";
}

int main(int argc, char** argv) {
    if (argc < 2) {
        printUsage();
        return 1;
    }

    std::string command = argv[1];
    juce::File currentDir = juce::File::getCurrentWorkingDirectory();

    if (command == "new") {
        if (argc < 3) {
            std::cerr << "Usage: frate new <pod_name>\n";
            return 1;
        }
        std::string podName = argv[2];
        bool isLib = (argc > 3 && std::string(argv[3]) == "--lib");
        
        frate::PodMetadata meta;
        meta.name = podName;
        meta.version = "1.0.0";
        meta.type = isLib ? "lib" : "bin";
        meta.description = "A new Frust pod.";
        
        juce::File targetDir = currentDir.getChildFile(juce::String(podName));
        if (frate::FratePodBuilder::scaffoldPod(targetDir, meta)) {
            std::cout << "Created " << meta.type << " pod '" << podName << "' in " << targetDir.getFullPathName() << "\n";
        } else {
            std::cerr << "Error: Failed to create pod directory.\n";
            return 1;
        }
    } 
    else if (command == "build" || command == "run") {
        juce::File frateJson = currentDir.getChildFile("frate.json");
        if (!frateJson.existsAsFile()) {
            std::cerr << "Error: No frate.json found in current directory.\n";
            return 1;
        }
        
        frate::FrateConfig config;
        config.load(frateJson);
        const auto& meta = config.getMetadata();
        
        std::map<std::string, juce::File> localWorkspaceMap;
        std::vector<juce::File> buildQueue;
        
        if (meta.isWorkspace) {
            std::cout << "Workspace detected. Members: " << meta.workspaceMembers.size() << "\n";
            for (const auto& memberPath : meta.workspaceMembers) {
                juce::File memberDir = currentDir.getChildFile(juce::String(memberPath));
                if (!memberDir.isDirectory()) {
                    std::cerr << "Error: Workspace member directory not found: " << memberDir.getFullPathName() << "\n";
                    return 1;
                }
                
                juce::File memberJson = memberDir.getChildFile("frate.json");
                if (!memberJson.existsAsFile()) {
                    std::cerr << "Error: Workspace member missing frate.json: " << memberDir.getFullPathName() << "\n";
                    return 1;
                }
                
                frate::FrateConfig memberConfig;
                memberConfig.load(memberJson);
                localWorkspaceMap[memberConfig.getMetadata().name] = memberDir;
                buildQueue.push_back(memberDir);
            }
        } else {
            buildQueue.push_back(currentDir);
        }
        
        for (const auto& podDir : buildQueue) {
            bool isRun = (command == "run") && (!meta.isWorkspace || podDir == buildQueue.back());
            std::vector<juce::String> wsObj; // collected objects, ignored here since buildPod links binaries internally
            if (!buildPod(podDir, isRun, localWorkspaceMap, wsObj)) {
                return 1;
            }
        }
    }
    else if (command == "package") {
        auto podJson = currentDir.getChildFile("frate.json");
        if (!podJson.existsAsFile()) {
            std::cerr << "Error: Current directory is not a valid pod (missing frate.json).\n";
            return 1;
        }
        
        frate::FrateConfig config;
        if (!config.load(podJson)) {
            std::cerr << "Error: Invalid frate.json.\n";
            return 1;
        }
        const auto& meta = config.getMetadata();
        if (meta.name.empty() || meta.version.empty()) {
            std::cerr << "Error: Invalid metadata in frate.json.\n";
            return 1;
        }
        
        juce::File frpod = frate::FratePodBuilder::packagePod(currentDir);
        if (frpod.existsAsFile()) {
            std::cout << "Packaged pod to " << frpod.getFileName() << "\n";
        } else {
            std::cerr << "Error: Packaging failed.\n";
            return 1;
        }
    }
    else if (command == "install") {
        auto podJson = currentDir.getChildFile("frate.json");
        if (!podJson.existsAsFile()) {
            std::cerr << "Error: Current directory is not a valid pod (missing frate.json).\n";
            return 1;
        }
        
        frate::FrateConfig config;
        if (!config.load(podJson)) {
            std::cerr << "Error: Invalid frate.json.\n";
            return 1;
        }
        const auto& meta = config.getMetadata();
        if (meta.name.empty() || meta.version.empty()) {
            std::cerr << "Error: Invalid metadata in frate.json.\n";
            return 1;
        }
        
        juce::String filename = juce::String(meta.name) + "-" + juce::String(meta.version) + ".frpod";
        auto outFile = currentDir.getParentDirectory().getChildFile(filename);
        
        if (!outFile.existsAsFile()) {
            std::cerr << "Error: Please package the pod first. Could not find " << filename << "\n";
            return 1;
        }
        
        frate::FrateCache cache;
        if (cache.installFromPackage(outFile, meta.name, meta.version)) {
            std::cout << "Successfully installed '" << meta.name << "' v" << meta.version << " to local cache.\n";
        } else {
            std::cerr << "Error: Install failed.\n";
            return 1;
        }
    }
    else if (command == "add") {
        if (argc < 4) {
            std::cerr << "Usage: frate add <pod_name> <version>\n";
            return 1;
        }
        std::string depName = argv[2];
        std::string depVersion = argv[3];
        
        frate::FrateConfig config;
        juce::File frateJson = currentDir.getChildFile("frate.json");
        config.load(frateJson); // Load if exists, otherwise starts empty
        
        frate::PodDependency dep;
        dep.name = depName;
        dep.version = depVersion;
        
        config.addDependency(dep);
        
        if (config.save(frateJson)) {
            std::cout << "Added dependency '" << depName << "' v" << depVersion << " to frate.json\n";
        } else {
            std::cerr << "Error: Failed to save frate.json\n";
            return 1;
        }
    }
    else if (command == "update") {
        auto podJson = currentDir.getChildFile("frate.json");
        if (!podJson.existsAsFile()) {
            std::cerr << "Error: Current directory is not a valid pod (missing frate.json).\n";
            return 1;
        }
        
        frate::FrateConfig config;
        if (!config.load(podJson)) {
            std::cerr << "Error: Invalid frate.json.\n";
            return 1;
        }
        
        frate::FrateCache cache;
        frate::FrateRegistryClient registry; // defaults to the live lagdaemon.com registry
        frate::FrateResolver resolver(cache, registry);

        bool anyFailed = false;
        for (const auto& dep : config.getDependencies()) {
            auto status = resolver.resolve(dep.name, dep.version);
            switch (status) {
                case frate::ResolveStatus::ResolvedFromCache:
                    std::cout << "Already up to date: " << dep.name << " v" << dep.version << "\n";
                    break;
                case frate::ResolveStatus::ResolvedFromRegistry:
                    std::cout << "Fetched and cached: " << dep.name << " v" << dep.version << "\n";
                    break;
                case frate::ResolveStatus::UnresolvedNotFound:
                    std::cerr << "Not found in registry: " << dep.name << " v" << dep.version << "\n";
                    anyFailed = true;
                    break;
                case frate::ResolveStatus::UnresolvedNetworkError:
                    std::cerr << "Network error fetching: " << dep.name << " v" << dep.version << "\n";
                    anyFailed = true;
                    break;
                case frate::ResolveStatus::UnresolvedExtractError:
                    std::cerr << "Failed to extract into cache: " << dep.name << " v" << dep.version << "\n";
                    anyFailed = true;
                    break;
            }
        }
        std::cout << "Update complete.\n";
        if (anyFailed) return 1;
    }
    else if (command == "publish") {
        auto podJson = currentDir.getChildFile("frate.json");
        if (!podJson.existsAsFile()) {
            std::cerr << "Error: Current directory is not a valid pod (missing frate.json).\n";
            return 1;
        }

        frate::FrateConfig config;
        if (!config.load(podJson)) {
            std::cerr << "Error: Invalid frate.json.\n";
            return 1;
        }
        const auto& meta = config.getMetadata();
        if (meta.name.empty() || meta.version.empty()) {
            std::cerr << "Error: Invalid metadata in frate.json.\n";
            return 1;
        }

        juce::String token = loadIdeAuthToken();
        if (token.isEmpty()) {
            std::cerr << "Error: Not signed in (or session expired). Sign in via the IDE's Account menu, then retry.\n";
            return 1;
        }

        juce::String filename = juce::String(meta.name) + "-" + juce::String(meta.version) + ".frpod";
        auto frpodFile = currentDir.getParentDirectory().getChildFile(filename);
        if (!frpodFile.existsAsFile()) {
            std::cerr << "Error: Please package the pod first. Could not find " << filename << "\n";
            return 1;
        }

        juce::String license = (argc > 2) ? juce::String(argv[2]) : juce::String("MIT");

        frate::FrateRegistryClient registry;
        registry.setAuthToken(token);

        std::cout << "Requesting upload URL for " << meta.name << " v" << meta.version << "...\n";
        auto [presignedUrl, s3Key] = registry.getUploadUrl(meta.name, meta.version);
        if (presignedUrl.isEmpty()) {
            std::cerr << "Error: Failed to get upload URL. Do you have the publisher role?\n";
            return 1;
        }

        std::cout << "Uploading " << filename << "...\n";
        if (!registry.uploadToS3(presignedUrl, frpodFile)) {
            std::cerr << "Error: Failed to upload to S3.\n";
            return 1;
        }

        frate::PodMetadata publishMeta;
        publishMeta.name = meta.name;
        publishMeta.version = meta.version;
        publishMeta.description = meta.description;
        publishMeta.exports = meta.exports;
        publishMeta.dependencies = meta.dependencies;

        if (registry.publishPod(publishMeta, s3Key, frpodFile.getSize(), license)) {
            std::cout << "Published " << meta.name << " v" << meta.version << " (license: " << license << ").\n";
        } else {
            std::cerr << "Error: Failed to publish metadata. Check if this version already exists.\n";
            return 1;
        }
    }
    else {
        std::cerr << "Unknown command: " << command << "\n";
        printUsage();
        return 1;
    }

    return 0;
}
