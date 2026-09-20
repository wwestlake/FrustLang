#include "EnvironmentCompile.h"

#include <algorithm>
#include <cstdlib>
#include <sstream>

namespace frust {
namespace {

// The value of "namespace" in a frate.json, without a JSON library: a compiler-level need
// only ("frust::core"-style qualification). Returns "" if there is none.
std::string NamespaceFromManifest(const std::string& json) {
    const std::string key = "\"namespace\"";
    auto at = json.find(key);
    if (at == std::string::npos) return {};
    at = json.find(':', at + key.size());
    if (at == std::string::npos) return {};
    const auto open = json.find('"', at);
    if (open == std::string::npos) return {};
    const auto close = json.find('"', open + 1);
    if (close == std::string::npos) return {};
    return json.substr(open + 1, close - open - 1);
}

// Numeric-aware version order: "1.10.0" > "1.9.0".
bool VersionLess(const std::string& a, const std::string& b) {
    std::istringstream sa(a), sb(b);
    std::string pa, pb;
    while (true) {
        const bool ha = static_cast<bool>(std::getline(sa, pa, '.'));
        const bool hb = static_cast<bool>(std::getline(sb, pb, '.'));
        if (!ha && !hb) return false;
        if (!ha) return true;
        if (!hb) return false;
        const long na = std::strtol(pa.c_str(), nullptr, 10);
        const long nb = std::strtol(pb.c_str(), nullptr, 10);
        if (na != nb) return na < nb;
    }
}

} // namespace

std::string NewestPodVersion(HostEnvironment& env, const std::string& podsRoot, const std::string& name) {
    std::vector<std::string> paths;
    if (!env.files.listFiles(JoinPath(podsRoot, name), paths)) return {};

    std::vector<std::string> versions;
    for (const auto& path : paths) {
        const auto slash = path.find('/');
        const std::string version = slash == std::string::npos ? path : path.substr(0, slash);
        if (std::find(versions.begin(), versions.end(), version) == versions.end()) versions.push_back(version);
    }
    if (versions.empty()) return {};
    return *std::max_element(versions.begin(), versions.end(), VersionLess);
}

bool ReadPodSource(HostEnvironment& env, const std::string& podsRoot, const std::string& name,
                   const std::string& version, PodSource& pod) {
    const std::string root = JoinPath(JoinPath(podsRoot, name), version);
    std::string manifest;
    if (!env.files.read(JoinPath(root, "frate.json"), manifest)) return false;
    pod.ns = NamespaceFromManifest(manifest);

    std::vector<std::string> paths;
    if (!env.files.listFiles(JoinPath(root, "src"), paths)) return false;
    for (const auto& relative : paths) {
        std::string text;
        if (!env.files.read(JoinPath(JoinPath(root, "src"), relative), text)) return false;
        pod.sources.push_back({ relative, std::move(text) });
    }
    return !pod.sources.empty();
}

bool MakeCompileRequest(HostEnvironment& env, const FileCompileRequest& request,
                        CompileRequest& out, std::string& error) {
    out = CompileRequest();
    out.podNamespace = request.podNamespace;
    out.emitObject = !request.outputPath.empty();
    out.captureIr = request.captureIr;

    if (request.sources.empty()) {
        error = "no source was given";
        return false;
    }
    for (const auto& path : request.sources) {
        SourceFile file;
        file.name = path;
        if (!env.files.read(path, file.text)) {
            error = "cannot read '" + path + "'";
            return false;
        }
        out.sources.push_back(std::move(file));
    }

    // `use self::x;` - a sibling file in the same virtual directory.
    HostEnvironment* environment = &env;
    out.siblingFiles = [environment](const std::string& name, std::string& text) {
        return environment->files.read(name, text);
    };

    // `use pod;` / `import pod, "v";` - pods under podsRoot.
    if (!request.podsRoot.empty()) {
        const std::string podsRoot = request.podsRoot;
        const auto dependencies = request.dependencies;
        out.pods = [environment, podsRoot, dependencies](const std::string& name, const std::string& requested,
                                                         PodSource& pod) {
            std::string version = requested;
            if (version.empty() || version == "current") {
                version.clear();
                for (const auto& dep : dependencies)
                    if (dep.first == name) { version = dep.second; break; }
                if (version.empty()) version = NewestPodVersion(*environment, podsRoot, name);
                if (version.empty()) return false;
            }
            return ReadPodSource(*environment, podsRoot, name, version, pod);
        };
    }
    return true;
}

CompileResult compileFiles(HostEnvironment& env, const FileCompileRequest& request) {
    CompileRequest compileRequest;
    std::string error;
    if (!MakeCompileRequest(env, request, compileRequest, error)) {
        CompileResult failed;
        Diagnostic d;
        d.phase = Diagnostic::Phase::Parser;
        d.message = error;
        failed.diagnostics.push_back(d);
        env.log.log(LogLevel::Error, "frust", error);
        return failed;
    }

    CompileResult result = Compile(compileRequest);

    for (const auto& d : result.diagnostics)
        env.log.log(d.severity == Diagnostic::Severity::Error ? LogLevel::Error : LogLevel::Warning, "frust",
                    FormatDiagnostic(d));

    if (result.ok && !request.outputPath.empty()) {
        const std::string bytes(reinterpret_cast<const char*>(result.object.data()), result.object.size());
        if (!env.files.write(request.outputPath, bytes)) {
            result.ok = false;
            Diagnostic d;
            d.phase = Diagnostic::Phase::Backend;
            d.message = "cannot write '" + request.outputPath + "'";
            result.diagnostics.push_back(d);
            env.log.log(LogLevel::Error, "frust", d.message);
        }
    }

    std::ostringstream summary;
    if (result.ok) {
        summary << "compiled " << request.sources.size() << " source(s)";
        if (!request.outputPath.empty()) summary << " -> " << request.outputPath << " (" << result.object.size() << " bytes)";
        env.log.log(LogLevel::Info, "frust", summary.str());
    } else {
        size_t errors = 0;
        for (const auto& d : result.diagnostics) if (d.severity == Diagnostic::Severity::Error) ++errors;
        summary << "compilation failed: " << errors << " error(s)";
        env.log.log(LogLevel::Error, "frust", summary.str());
    }
    return result;
}

} // namespace frust
