#pragma once

// Frate over a HostEnvironment: every pod operation reads and writes through the
// environment (the disk for the command line, the VFS for the Suite, memory for
// tests) and logs to the environment's log. Nothing here touches a real folder, the
// console, or another program on its own.
//
// Pod layout in an environment, as virtual paths:
//   <podRoot>/frate.json, <podRoot>/src/lib.fr (or main.fr), <podRoot>/build/<name>.o,
//   <podRoot>/dist/<name>-<version>.frpod
// and installed (downloaded) pods under a pods root:
//   <podsRoot>/<name>/<version>/frate.json, .../src/...

#include <frate/PodBuild.h>

#include <EnvironmentCompile.h>
#include <HostEnvironment.h>

#include <string>

namespace frate {

// Reads a pod's frate.json and its .fr / .fri files at any depth (build output is left alone).
bool readPodFiles(frust::HostEnvironment& env, const std::string& podRoot, PodFiles& files, std::string& error);

// Writes a pod's files under podRoot.
bool writePodFiles(frust::HostEnvironment& env, const std::string& podRoot, const PodFiles& files, std::string& error);

struct PodEnvironmentBuildOptions {
    std::string podRoot;        // the pod being built
    std::string podsRoot;       // where its dependencies are installed
    bool emitObject = true;     // false: check only
};

// `frate build`, in the environment: reads the pod, builds it in memory, writes
// <podRoot>/build/<name>.o, and logs each step. The result carries the diagnostics.
PodBuildResult buildPodInEnvironment(frust::HostEnvironment& env, const PodEnvironmentBuildOptions& options);

// `frate package`: writes <podRoot>/dist/<name>-<version>.frpod and returns its path.
bool packPodInEnvironment(frust::HostEnvironment& env, const std::string& podRoot,
                          std::string& packagePath, std::string& error);

// `frate install`: unpacks .frpod bytes into <podsRoot>/<name>/<version>/ and reports which pod it was.
bool installPodPackage(frust::HostEnvironment& env, const std::string& podsRoot, const std::string& frpodBytes,
                       std::string& name, std::string& version, std::string& error);

} // namespace frate
