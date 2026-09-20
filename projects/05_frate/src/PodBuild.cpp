#include <frate/PodBuild.h>

#include <frate/FrateConfig.h>

namespace frate {
namespace {

void addDiagnostic(PodBuildResult& result, std::string message) {
    frust::Diagnostic d;
    d.severity = frust::Diagnostic::Severity::Error;
    d.phase = frust::Diagnostic::Phase::Modules;
    d.message = std::move(message);
    result.diagnostics.push_back(std::move(d));
}

bool startsWith(const std::string& text, const std::string& prefix) {
    return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
}

} // namespace

bool findPodEntry(const PodFiles& pod, PodEntry& entry, std::string& error) {
    const auto manifest = pod.find("frate.json");
    if (manifest == pod.end()) {
        error = "the pod has no frate.json";
        return false;
    }
    FrateConfig config;
    if (!config.loadFromString(manifest->second)) {
        error = "frate.json is not valid";
        return false;
    }
    const std::string path = config.getMetadata().type == "lib" ? "src/lib.fr" : "src/main.fr";
    const auto file = pod.find(path);
    if (file == pod.end()) {
        error = "the pod's entry point " + path + " was not found";
        return false;
    }
    entry = { path, file->second };
    return true;
}

PodBuildResult buildPod(const PodFiles& pod, PodSource* dependencies, const PodBuildOptions& options) {
    PodBuildResult result;

    std::string error;
    PodEntry entry;
    if (!findPodEntry(pod, entry, error)) {
        addDiagnostic(result, error);
        return result;
    }

    FrateConfig config;
    config.loadFromString(pod.at("frate.json"));
    const auto& meta = config.getMetadata();
    result.name = meta.name;
    result.version = meta.version;
    result.type = meta.type;

    frust::CompileRequest request;
    request.sources.push_back({ entry.name, entry.text });
    request.emitObject = options.emitObject;

    // `use self::x;` names a file next to the entry file, inside this pod.
    request.siblingFiles = [&pod](const std::string& name, std::string& text) {
        const auto found = pod.find(name);
        if (found == pod.end()) return false;
        text = found->second;
        return true;
    };

    // `use pod;` and `import pod, "version";` go to the host's pod source.
    // The version comes from the import itself, or, for a bare `use`, from
    // this pod's own dependency list.
    request.pods = [&config, dependencies](const std::string& name, const std::string& requested,
                                           frust::PodSource& out) {
        if (dependencies == nullptr) return false;

        std::string version = requested;
        if (version.empty() || version == "current") {
            version.clear();
            for (const auto& dep : config.getDependencies())
                if (dep.name == name) { version = dep.version; break; }
            if (version.empty()) return false; // not a declared dependency
        }

        PodFiles files;
        if (!dependencies->findPod(name, version, files)) return false;

        FrateConfig depConfig;
        const auto manifest = files.find("frate.json");
        if (manifest != files.end() && depConfig.loadFromString(manifest->second))
            out.ns = depConfig.getMetadata().namespacePath;

        for (const auto& [path, contents] : files)
            if (startsWith(path, "src/")) out.sources.push_back({ path.substr(4), contents });
        return true;
    };

    auto compiled = frust::Compile(request);
    result.diagnostics = std::move(compiled.diagnostics);
    result.object = std::move(compiled.object);
    result.ok = compiled.ok;
    return result;
}

} // namespace frate
