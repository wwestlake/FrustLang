#pragma once

// Building a pod entirely in memory: the pod's files in, diagnostics and the
// object file out. No folder is read or written, and no other program is
// started. This is `frate build` for a host that keeps pods somewhere other
// than the disk (Djehuti Station keeps them in its VFS container).
//
// The command-line `frate` still works on real folders; it is a developer's
// terminal tool, not part of the suite's storage.

#include <frate/PodArchive.h>

#include <CompilerApi.h>

#include <cstdint>
#include <string>
#include <vector>

namespace frate {

// Where the pods a build depends on come from. The host implements it over
// its own storage.
class PodSource {
public:
    virtual ~PodSource() = default;

    // The files of pod `name` at exactly `version`. False if the host does not have it.
    virtual bool findPod(const std::string& name, const std::string& version, PodFiles& files) = 0;
};

struct PodBuildResult {
    bool ok = false;
    std::vector<frust::Diagnostic> diagnostics;
    std::vector<std::uint8_t> object;   // the pod's object file, when ok and an object was asked for
    std::string name;                   // from frate.json
    std::string version;
    std::string type;                   // "lib" or "bin"
};

struct PodBuildOptions {
    bool emitObject = true;   // false: check only (parse and generate, no object)
};

// Builds one pod. `dependencies` supplies the pods named by `use pod;` and
// `import pod, "version";` (a bare `use pod;` takes its version from this
// pod's frate.json; a name that is not a declared dependency is left alone).
// It may be null for a pod with no dependencies. The compile is of the pod's
// own entry file (src/lib.fr, or src/main.fr for a bin) and the `use self::x;`
// files it names, with imported pods' sources merged in, exactly as `frate build`
// does; dependencies that are not imported are not compiled.
PodBuildResult buildPod(const PodFiles& pod, PodSource* dependencies, const PodBuildOptions& options = {});

// The files a pod loads at run time by name (for the plugin host's load-from-text):
// the entry file's text, and the sibling files it names.
struct PodEntry {
    std::string name;   // "src/lib.fr" or "src/main.fr"
    std::string text;
};
bool findPodEntry(const PodFiles& pod, PodEntry& entry, std::string& error);

} // namespace frate
