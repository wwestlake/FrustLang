// Checks the FRust libraries over a HostEnvironment (compiler and Frate): everything is read
// and written through the environment, the log goes to the environment's log, and no real
// file is touched. Exit code 0 = pass.

#include <frate/PodEnvironment.h>
#include <frate/PodMetadata.h>

#include "EnvironmentCompile.h"
#include "MemoryEnvironment.h"

#include <filesystem>
#include <iostream>
#include <set>
#include <string>

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    std::cout << (ok ? "PASS  " : "FAIL  ") << what << std::endl;
    if (!ok) ++failures;
}

std::set<std::string> listRealFiles(const std::filesystem::path& dir) {
    std::set<std::string> names;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        const auto ext = entry.path().extension().string();
        if (ext == ".o" || ext == ".obj" || ext == ".ll" || ext == ".fr" || ext == ".frust" || ext == ".frpod" || ext == ".log")
            names.insert(entry.path().filename().string());
    }
    return names;
}

const char* kGood = "pub fn gain(input: f64, amount: f64) -> f64 = {\n    input * amount\n}\n";

} // namespace

int main() {
    const auto cwdBefore = listRealFiles(std::filesystem::current_path());
    const auto tmpBefore = listRealFiles(std::filesystem::temp_directory_path());

    // ---- Path helpers ----
    {
        std::string out;
        check(frust::NormalizePath("a\\b/./c//d.fr", out) && out == "a/b/c/d.fr", "paths normalize (backslash, dot, doubled slash)");
        check(!frust::NormalizePath("a/../b", out), "a path with .. is refused");
        check(!frust::NormalizePath("/abs/path", out), "an absolute path is refused");
        check(!frust::NormalizePath("C:/x", out), "a drive-letter path is refused");
        check(frust::JoinPath("a/b", "c") == "a/b/c" && frust::JoinPath("", "c") == "c" && frust::JoinPath("a", "/c") == "a/c", "paths join");
        check(frust::DirectoryOf("a/b/c.fr") == "a/b" && frust::FileNameOf("a/b/c.fr") == "c.fr", "directory and file name split");
    }

    // ---- Memory environment behaves as a file system ----
    {
        frust::MemoryEnvironment mem;
        auto& fs = mem.fileSystem;
        check(fs.write("x/y/z.txt", "hi") && fs.exists("x/y/z.txt"), "write then exists");
        std::string bytes;
        check(fs.read("x/y/z.txt", bytes) && bytes == "hi", "read returns what was written");
        std::vector<std::string> listed;
        fs.write("x/y/w.txt", "w");
        fs.write("other/q.txt", "q");
        check(fs.listFiles("x", listed) && listed.size() == 2 && listed[0] == "y/w.txt", "listFiles lists below a directory, relative");
        check(fs.remove("x/y/w.txt") && !fs.exists("x/y/w.txt"), "remove");
    }

    // ---- compileFiles: source in the environment, object and log back in it ----
    {
        frust::MemoryEnvironment mem;
        mem.fileSystem.write("Code/scripts/gain.fr", kGood);
        auto env = mem.environment();

        frust::FileCompileRequest request;
        request.sources = { "Code/scripts/gain.fr" };
        request.outputPath = "Code/build/gain.o";
        const auto result = frust::compileFiles(env, request);
        check(result.ok, "compileFiles compiles a source read from the environment");
        std::string object;
        check(mem.fileSystem.read("Code/build/gain.o", object) && object.size() > 100 && object[0] == 0x64,
              "the object file is written into the environment");
        check(mem.logSink.contains("compiled 1 source(s)") && mem.logSink.contains("Code/build/gain.o"),
              "the log receives a summary line");

        // Check-only writes no object.
        frust::FileCompileRequest checkOnly;
        checkOnly.sources = { "Code/scripts/gain.fr" };
        check(frust::compileFiles(env, checkOnly).ok && !mem.fileSystem.exists("Code/build/none.o"), "check-only succeeds without an object");

        // An error goes to the result AND the log, with file and line.
        mem.fileSystem.write("Code/scripts/bad.fr", "pub fn f() -> i64 = {\n    let x = ;\n}\n");
        frust::FileCompileRequest bad;
        bad.sources = { "Code/scripts/bad.fr" };
        bad.outputPath = "Code/build/bad.o";
        const auto failed = frust::compileFiles(env, bad);
        check(!failed.ok && !mem.fileSystem.exists("Code/build/bad.o"), "a failed compile writes no object");
        check(mem.logSink.contains("Code/scripts/bad.fr:2:") && mem.logSink.contains("compilation failed"),
              "the error is logged with the virtual path and line");

        // A source the environment does not have.
        frust::FileCompileRequest missing;
        missing.sources = { "Code/scripts/nothing.fr" };
        const auto none = frust::compileFiles(env, missing);
        check(!none.ok && mem.logSink.contains("cannot read 'Code/scripts/nothing.fr'"), "a missing source is reported, not a crash");
    }

    // ---- use self:: reads the sibling from the same virtual directory ----
    {
        frust::MemoryEnvironment mem;
        mem.fileSystem.write("Code/p/main.fr", "use self::helper;\npub fn quad(x: i64) -> i64 = twice(twice(x))\n");
        mem.fileSystem.write("Code/p/helper.fr", "pub fn twice(x: i64) -> i64 = x * 2\n");
        auto env = mem.environment();
        frust::FileCompileRequest request;
        request.sources = { "Code/p/main.fr" };
        check(frust::compileFiles(env, request).ok, "use self:: is read through the environment");
    }

    // ---- Pods resolve from a pods root in the environment ----
    {
        frust::MemoryEnvironment mem;
        auto& fs = mem.fileSystem;
        fs.write("pods/mathpod/1.0.0/frate.json", "{\"name\":\"mathpod\",\"version\":\"1.0.0\",\"type\":\"lib\"}");
        fs.write("pods/mathpod/1.0.0/src/lib.fr", "use self::ops;\n");
        fs.write("pods/mathpod/1.0.0/src/ops.fr", "pub fn three() -> i64 = 3\n");
        fs.write("pods/mathpod/1.2.0/frate.json", "{\"name\":\"mathpod\",\"version\":\"1.2.0\",\"type\":\"lib\"}");
        fs.write("pods/mathpod/1.2.0/src/lib.fr", "use self::ops;\n");
        fs.write("pods/mathpod/1.2.0/src/ops.fr", "pub fn three() -> i64 = 3\n");
        fs.write("Code/app.fr", "use mathpod;\npub fn f() -> i64 = mathpod::three()\n");
        fs.write("Code/app2.fr", "import mathpod, \"1.0.0\";\npub fn f() -> i64 = mathpod::three()\n");
        auto env = mem.environment();

        check(frust::NewestPodVersion(env, "pods", "mathpod") == "1.2.0", "the newest pod version is found");

        frust::FileCompileRequest useForm;
        useForm.sources = { "Code/app.fr" };
        useForm.podsRoot = "pods";
        check(frust::compileFiles(env, useForm).ok, "a bare 'use pod;' resolves from the pods root");

        frust::FileCompileRequest importForm;
        importForm.sources = { "Code/app2.fr" };
        importForm.podsRoot = "pods";
        check(frust::compileFiles(env, importForm).ok, "import pod, \"version\" resolves from the pods root");

        frust::FileCompileRequest badVersion;
        badVersion.sources = { "Code/app2.fr" };
        badVersion.podsRoot = "pods";
        fs.write("Code/app3.fr", "import mathpod, \"9.9.9\";\npub fn f() -> i64 = 1\n");
        badVersion.sources = { "Code/app3.fr" };
        check(!frust::compileFiles(env, badVersion).ok, "an import of a version that is not there fails");
    }

    // ---- Frate over the environment: new pod, build, package, install ----
    {
        frust::MemoryEnvironment mem;
        auto& fs = mem.fileSystem;
        auto env = mem.environment();

        frate::PodMetadata meta;
        meta.name = "fresh";
        meta.version = "0.1.0";
        meta.type = "lib";
        std::string error;
        check(frate::writePodFiles(env, "Code/fresh", frate::scaffoldPodFiles(meta), error), "a pod scaffold is written into the environment");

        frate::PodEnvironmentBuildOptions options;
        options.podRoot = "Code/fresh";
        options.podsRoot = "pods";
        const auto built = frate::buildPodInEnvironment(env, options);
        check(built.ok && fs.exists("Code/fresh/build/fresh.o"), "a pod builds and its object lands in the environment");
        check(mem.logSink.contains("building pod at Code/fresh") && mem.logSink.contains("built fresh 0.1.0"),
              "Frate logs each step to the environment's log");

        std::string packagePath;
        check(frate::packPodInEnvironment(env, "Code/fresh", packagePath, error) && packagePath == "Code/fresh/dist/fresh-0.1.0.frpod"
                  && fs.exists(packagePath), "a pod packages into the environment");

        std::string bytes, name, version;
        fs.read(packagePath, bytes);
        check(frate::installPodPackage(env, "pods", bytes, name, version, error) && name == "fresh" && version == "0.1.0"
                  && fs.exists("pods/fresh/0.1.0/frate.json") && fs.exists("pods/fresh/0.1.0/src/lib.fr"),
              "a package installs under the pods root");

        // A second pod that uses the first, through the pods root.
        fs.write("Code/user/frate.json", "{\"name\":\"user\",\"version\":\"0.1.0\",\"type\":\"lib\",\"dependencies\":[{\"name\":\"fresh\",\"version\":\"0.1.0\"}]}");
        fs.write("Code/user/src/lib.fr", "use fresh;\npub fn f() -> i64 = 1\n");
        frate::PodEnvironmentBuildOptions userOptions;
        userOptions.podRoot = "Code/user";
        userOptions.podsRoot = "pods";
        check(frate::buildPodInEnvironment(env, userOptions).ok, "a pod that uses an installed pod builds");

        // A broken pod: diagnostics come back and go to the log, and no object is written.
        fs.write("Code/broken/frate.json", "{\"name\":\"broken\",\"version\":\"0.1.0\",\"type\":\"lib\"}");
        fs.write("Code/broken/src/lib.fr", "pub fn f() -> i64 = {\n    let x = ;\n}\n");
        frate::PodEnvironmentBuildOptions brokenOptions;
        brokenOptions.podRoot = "Code/broken";
        const auto broken = frate::buildPodInEnvironment(env, brokenOptions);
        check(!broken.ok && !fs.exists("Code/broken/build/broken.o") && mem.logSink.contains("build of broken failed"),
              "a broken pod fails, logs it, and writes no object");

        // Not a pod.
        frate::PodEnvironmentBuildOptions notAPod;
        notAPod.podRoot = "Code/empty";
        check(!frate::buildPodInEnvironment(env, notAPod).ok, "a folder that is not a pod is reported");
    }

    // ---- Nothing real was touched ----
    check(listRealFiles(std::filesystem::current_path()) == cwdBefore, "the working directory gained no file");
    check(listRealFiles(std::filesystem::temp_directory_path()) == tmpBefore, "the temp directory gained no file");

    std::cout << (failures == 0 ? "ALL PASSED" : "FAILURES: " + std::to_string(failures)) << std::endl;
    return failures == 0 ? 0 : 1;
}
