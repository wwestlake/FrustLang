#pragma once

// The compiler over a HostEnvironment: source files in, object file out, log
// lines to the environment's log - all through the environment, none directly.
//
// This is the layer an application uses. It sits on frust::Compile (CompilerApi.h),
// which is the same compiler with no I/O at all.

#include "CompilerApi.h"
#include "HostEnvironment.h"

#include <string>
#include <utility>
#include <vector>

namespace frust {

struct FileCompileRequest {
    // Virtual paths of the sources, compiled together as one unit. `use self::x;` in a source
    // is read from x.frust / x.fr in the same virtual directory.
    std::vector<std::string> sources;

    // Where the object file is written. Empty: check only, no object.
    std::string outputPath;

    std::string podNamespace;

    // Where pods live, as <podsRoot>/<name>/<version>/{frate.json, src/...}. Used to resolve
    // `use pod;` and `import pod, "version";`. Empty: no pods.
    std::string podsRoot;

    // Pinned (name, version) pairs, as a pod's frate.json lists them. A bare `use pod;` takes its
    // version from here; if the pod is not listed, the newest version under podsRoot is used.
    std::vector<std::pair<std::string, std::string>> dependencies;

    bool captureIr = false;
};

// Builds the CompileRequest the environment implies: reads every source through env.files, and
// wires sibling-file and pod lookups to env.files. False, with a message, if a source cannot be read.
bool MakeCompileRequest(HostEnvironment& env, const FileCompileRequest& request,
                        CompileRequest& out, std::string& error);

// Compiles. Diagnostics come back in the result AND go to env.log, one line each, followed by a
// one-line summary. The object is written to request.outputPath through env.files.
CompileResult compileFiles(HostEnvironment& env, const FileCompileRequest& request);

// The newest version of pod `name` that exists under podsRoot ("" if there is none).
std::string NewestPodVersion(HostEnvironment& env, const std::string& podsRoot, const std::string& name);

// Reads pod `name` at `version` from <podsRoot>/<name>/<version>/ into a PodSource (its src/ files
// and its namespace from frate.json). False if it is not there.
bool ReadPodSource(HostEnvironment& env, const std::string& podsRoot, const std::string& name,
                   const std::string& version, PodSource& pod);

} // namespace frust
