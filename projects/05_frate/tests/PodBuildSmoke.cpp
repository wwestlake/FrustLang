// Checks building a pod entirely in memory (frate/PodBuild.h, frate/PodArchive.h):
// the pod's files in, diagnostics and the object file out, and that no file is
// created anywhere. Exit code 0 = pass.

#include <frate/PodArchive.h>
#include <frate/PodBuild.h>
#include <frate/PodMetadata.h>

#include <filesystem>
#include <iostream>
#include <map>
#include <set>
#include <string>

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    std::cout << (ok ? "PASS  " : "FAIL  ") << what << std::endl;
    if (!ok) ++failures;
}

std::set<std::string> listFiles(const std::filesystem::path& dir) {
    std::set<std::string> names;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        const auto ext = entry.path().extension().string();
        if (ext == ".o" || ext == ".obj" || ext == ".ll" || ext == ".fr" || ext == ".frust" || ext == ".frpod" || ext == ".json")
            names.insert(entry.path().filename().string());
    }
    return names;
}

// A pod store that lives in a map.
class MapPods final : public frate::PodSource {
public:
    std::map<std::string, frate::PodFiles> pods; // "name@version"
    int lookups = 0;

    bool findPod(const std::string& name, const std::string& version, frate::PodFiles& files) override {
        ++lookups;
        const auto found = pods.find(name + "@" + version);
        if (found == pods.end()) return false;
        files = found->second;
        return true;
    }
};

frate::PodFiles libPod(const std::string& name, const std::string& deps, const std::string& lib,
                       const std::map<std::string, std::string>& extra = {}) {
    frate::PodFiles pod;
    pod["frate.json"] = "{\"name\":\"" + name + "\",\"version\":\"1.0.0\",\"type\":\"lib\",\"dependencies\":[" + deps + "]}";
    pod["src/lib.fr"] = lib;
    for (const auto& [path, text] : extra) pod[path] = text;
    return pod;
}

} // namespace

int main() {
    const auto cwd = std::filesystem::current_path();
    const auto tmp = std::filesystem::temp_directory_path();
    const auto cwdBefore = listFiles(cwd);
    const auto tmpBefore = listFiles(tmp);

    // 1. A single-file library pod builds to an object in memory.
    {
        const auto pod = libPod("gain", "", "pub fn gain(x: f64, k: f64) -> f64 = x * k\n");
        const auto r = frate::buildPod(pod, nullptr);
        check(r.ok, "a library pod builds");
        check(r.name == "gain" && r.version == "1.0.0" && r.type == "lib", "the pod's name, version and type come back");
        check(r.object.size() > 100 && r.object[0] == 0x64 && r.object[1] == 0x86, "the object file comes back as x64 COFF bytes");
    }

    // 2. A pod spanning several files through `use self::`.
    {
        const auto pod = libPod("multi", "", "use self::helper;\npub fn quad(x: i64) -> i64 = twice(twice(x))\n",
                                { { "src/helper.fr", "pub fn twice(x: i64) -> i64 = x * 2\n" } });
        check(frate::buildPod(pod, nullptr).ok, "use self:: resolves inside the pod");

        const auto missing = libPod("multi", "", "use self::nothere;\npub fn f() -> i64 = 1\n");
        const auto r = frate::buildPod(missing, nullptr);
        check(!r.ok && !r.diagnostics.empty(), "a use self:: naming a missing file fails with a diagnostic");
    }

    // 3. Errors come back with the file and line.
    {
        const auto pod = libPod("bad", "", "pub fn f() -> i64 = {\n    let x = ;\n}\n");
        const auto r = frate::buildPod(pod, nullptr);
        check(!r.ok && r.object.empty(), "a syntax error fails and gives no object");
        check(!r.diagnostics.empty() && r.diagnostics[0].file == "src/lib.fr" && r.diagnostics[0].line == 2,
              "the diagnostic names src/lib.fr and line 2");
    }

    // 4. Dependencies come from the host's pod source, by the version frate.json declares.
    {
        MapPods store;
        store.pods["mathpod@1.0.0"] = libPod("mathpod", "", "use self::ops;\n", { { "src/ops.fr", "pub fn three() -> i64 = 3\n" } });

        const auto useForm = libPod("app", "{\"name\":\"mathpod\",\"version\":\"1.0.0\"}",
                                    "use mathpod;\npub fn f() -> i64 = three()\n");
        const auto r = frate::buildPod(useForm, &store);
        check(r.ok && store.lookups >= 1, "a bare 'use pod;' takes the version from frate.json and merges the pod");

        const auto importForm = libPod("app2", "", "import mathpod, \"1.0.0\";\npub fn f() -> i64 = three()\n");
        check(frate::buildPod(importForm, &store).ok, "import pod, \"version\" merges the pod");

        const auto noSuch = libPod("app3", "", "import mathpod, \"9.9.9\";\npub fn f() -> i64 = 1\n");
        const auto bad = frate::buildPod(noSuch, &store);
        check(!bad.ok, "an import of a version the host does not have is an error");

        const auto undeclared = libPod("app4", "", "use mathpod;\npub fn f() -> i64 = 1\n");
        check(frate::buildPod(undeclared, &store).ok, "a bare 'use' of a pod that is not a declared dependency is left alone");
    }

    // 5. Check-only builds no object.
    {
        frate::PodBuildOptions options;
        options.emitObject = false;
        const auto r = frate::buildPod(libPod("g", "", "pub fn f() -> i64 = 1\n"), nullptr, options);
        check(r.ok && r.object.empty(), "check-only succeeds without an object");
    }

    // 6. A missing entry file or manifest is a diagnostic, not a crash.
    {
        frate::PodFiles noEntry;
        noEntry["frate.json"] = "{\"name\":\"x\",\"version\":\"1.0.0\",\"type\":\"lib\"}";
        check(!frate::buildPod(noEntry, nullptr).ok, "a pod with no src/lib.fr fails");
        check(!frate::buildPod(frate::PodFiles{}, nullptr).ok, "a pod with no frate.json fails");
    }

    // 7. Packaging round trip, in memory.
    {
        const auto pod = libPod("pack", "", "use self::helper;\npub fn f() -> i64 = twice(1)\n",
                                { { "src/helper.fr", "pub fn twice(x: i64) -> i64 = x * 2\n" },
                                  { "notes.txt", "not source, not packaged" } });
        std::string bytes, error;
        check(frate::packPod(pod, bytes, error) && bytes.size() > 50, "a pod packs into .frpod bytes");

        std::string again;
        frate::packPod(pod, again, error);
        check(bytes == again, "packing the same pod twice gives identical bytes");

        frate::PodFiles unpacked;
        check(frate::unpackPod(bytes.data(), bytes.size(), unpacked, error), "the bytes unpack");
        check(unpacked.count("frate.json") == 1 && unpacked.count("src/lib.fr") == 1 && unpacked.count("src/helper.fr") == 1,
              "the unpacked pod has its manifest and source files");
        check(unpacked.count("notes.txt") == 0, "files that are not .fr/.fri are not packaged");
        check(frate::buildPod(unpacked, nullptr).ok, "an unpacked pod builds");

        frate::PodFiles noManifest;
        noManifest["src/lib.fr"] = "x";
        check(!frate::packPod(noManifest, bytes, error), "packing a pod with no frate.json is refused");
        check(!frate::unpackPod("this is not a zip", 17, unpacked, error), "bytes that are not a package are refused");
    }

    // 8. Scaffolding.
    {
        frate::PodMetadata meta;
        meta.name = "fresh";
        meta.version = "0.1.0";
        meta.type = "lib";
        const auto files = frate::scaffoldPodFiles(meta);
        check(files.count("frate.json") == 1 && files.count("src/lib.fr") == 1, "a scaffold has a manifest and an entry file");
        check(frate::buildPod(files, nullptr).ok, "a fresh scaffold builds");
    }

    // Nothing was written anywhere.
    check(listFiles(cwd) == cwdBefore, "the working directory gained no file");
    check(listFiles(tmp) == tmpBefore, "the temp directory gained no file");

    std::cout << (failures == 0 ? "ALL PASSED" : "FAILURES: " + std::to_string(failures)) << std::endl;
    return failures == 0 ? 0 : 1;
}
