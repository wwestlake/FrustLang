#include <frate/PodEnvironment.h>

#include <frate/FrateConfig.h>

namespace frate {
namespace {

bool endsWith(const std::string& text, const char* suffix) {
    const std::string s(suffix);
    return text.size() >= s.size() && text.compare(text.size() - s.size(), s.size(), s) == 0;
}

bool isPodSourceFile(const std::string& relative) {
    return relative == "frate.json" || endsWith(relative, ".fr") || endsWith(relative, ".fri");
}

// Dependencies come from pods installed under podsRoot.
class EnvironmentPodSource final : public PodSource {
public:
    EnvironmentPodSource(frust::HostEnvironment& environment, std::string root)
        : env(environment), podsRoot(std::move(root)) {}

    bool findPod(const std::string& name, const std::string& version, PodFiles& files) override {
        std::string error;
        return readPodFiles(env, frust::JoinPath(frust::JoinPath(podsRoot, name), version), files, error);
    }

private:
    frust::HostEnvironment& env;
    std::string podsRoot;
};

} // namespace

bool readPodFiles(frust::HostEnvironment& env, const std::string& podRoot, PodFiles& files, std::string& error) {
    std::vector<std::string> relative;
    if (!env.files.listFiles(podRoot, relative)) {
        error = "cannot list '" + podRoot + "'";
        return false;
    }

    PodFiles result;
    for (const auto& path : relative) {
        if (!isPodSourceFile(path)) continue;
        std::string bytes;
        if (!env.files.read(frust::JoinPath(podRoot, path), bytes)) {
            error = "cannot read '" + frust::JoinPath(podRoot, path) + "'";
            return false;
        }
        result[path] = std::move(bytes);
    }
    if (result.find("frate.json") == result.end()) {
        error = "'" + podRoot + "' is not a pod (no frate.json)";
        return false;
    }
    files = std::move(result);
    return true;
}

bool writePodFiles(frust::HostEnvironment& env, const std::string& podRoot, const PodFiles& files, std::string& error) {
    for (const auto& [path, bytes] : files) {
        std::string safe;
        if (!frust::NormalizePath(path, safe) || safe.empty()) {
            error = "unsafe pod path '" + path + "'";
            return false;
        }
        if (!env.files.write(frust::JoinPath(podRoot, safe), bytes)) {
            error = "cannot write '" + frust::JoinPath(podRoot, safe) + "'";
            return false;
        }
    }
    return true;
}

PodBuildResult buildPodInEnvironment(frust::HostEnvironment& env, const PodEnvironmentBuildOptions& options) {
    PodBuildResult result;
    auto fail = [&](const std::string& message) {
        frust::Diagnostic d;
        d.phase = frust::Diagnostic::Phase::Modules;
        d.message = message;
        result.diagnostics.push_back(d);
        env.log.log(frust::LogLevel::Error, "frate", message);
        return result;
    };

    PodFiles files;
    std::string error;
    if (!readPodFiles(env, options.podRoot, files, error)) return fail(error);

    env.log.log(frust::LogLevel::Info, "frate", "building pod at " + options.podRoot);

    EnvironmentPodSource dependencies(env, options.podsRoot);
    PodBuildOptions buildOptions;
    buildOptions.emitObject = options.emitObject;
    result = buildPod(files, options.podsRoot.empty() ? nullptr : &dependencies, buildOptions);

    for (const auto& d : result.diagnostics)
        env.log.log(d.severity == frust::Diagnostic::Severity::Error ? frust::LogLevel::Error : frust::LogLevel::Warning,
                    "frate", frust::FormatDiagnostic(d));

    if (!result.ok) {
        env.log.log(frust::LogLevel::Error, "frate", "build of " + (result.name.empty() ? options.podRoot : result.name) + " failed");
        return result;
    }

    if (options.emitObject) {
        const std::string objectPath = frust::JoinPath(frust::JoinPath(options.podRoot, "build"), result.name + ".o");
        const std::string bytes(reinterpret_cast<const char*>(result.object.data()), result.object.size());
        if (!env.files.write(objectPath, bytes)) {
            result.ok = false;
            return fail("cannot write '" + objectPath + "'");
        }
        env.log.log(frust::LogLevel::Info, "frate",
                    "built " + result.name + " " + result.version + " -> " + objectPath + " (" + std::to_string(bytes.size()) + " bytes)");
    } else {
        env.log.log(frust::LogLevel::Info, "frate", "checked " + result.name + " " + result.version);
    }
    return result;
}

bool packPodInEnvironment(frust::HostEnvironment& env, const std::string& podRoot,
                          std::string& packagePath, std::string& error) {
    PodFiles files;
    if (!readPodFiles(env, podRoot, files, error)) return false;

    FrateConfig config;
    if (!config.loadFromString(files["frate.json"]) || config.getMetadata().name.empty() || config.getMetadata().version.empty()) {
        error = "frate.json is missing a name or version";
        return false;
    }

    std::string bytes;
    if (!packPod(files, bytes, error)) return false;

    packagePath = frust::JoinPath(frust::JoinPath(podRoot, "dist"),
                                  config.getMetadata().name + "-" + config.getMetadata().version + ".frpod");
    if (!env.files.write(packagePath, bytes)) {
        error = "cannot write '" + packagePath + "'";
        return false;
    }
    env.log.log(frust::LogLevel::Info, "frate", "packaged " + packagePath + " (" + std::to_string(bytes.size()) + " bytes)");
    return true;
}

bool installPodPackage(frust::HostEnvironment& env, const std::string& podsRoot, const std::string& frpodBytes,
                       std::string& name, std::string& version, std::string& error) {
    PodFiles files;
    if (!unpackPod(frpodBytes.data(), frpodBytes.size(), files, error)) return false;

    FrateConfig config;
    if (!config.loadFromString(files["frate.json"]) || config.getMetadata().name.empty() || config.getMetadata().version.empty()) {
        error = "the package's frate.json is missing a name or version";
        return false;
    }
    name = config.getMetadata().name;
    version = config.getMetadata().version;

    const std::string root = frust::JoinPath(frust::JoinPath(podsRoot, name), version);
    if (!writePodFiles(env, root, files, error)) return false;
    env.log.log(frust::LogLevel::Info, "frate", "installed " + name + " " + version + " -> " + root);
    return true;
}

} // namespace frate
