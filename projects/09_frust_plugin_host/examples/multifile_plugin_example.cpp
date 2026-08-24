// Verification harness for LANGUAGE_GAPS.md #8: a plugin whose main
// file uses `use self::helper;` to pull in a sibling file's
// declarations - frust_plugin_load previously read exactly one file
// and treated `use self::X;` as a no-op (ModuleLoader.cpp's own
// comment on ResolveImports said as much), so a plugin split across
// files silently lost everything the sibling file declared. Takes the
// MAIN file's path; the sibling `helper.frust` is expected to sit next
// to it (same directory) - see ../examples/multifile_main.frust /
// multifile_helper.frust for the real fixture this ships with.

#include "frust_plugin_host/FrustPluginHost.h"

#include <cstdint>
#include <cstdio>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: multifile_plugin_example <main.frust>\n");
        return 1;
    }

    FrustPluginHandle h = frust_plugin_load(argv[1]);
    if (!h) {
        std::fprintf(stderr, "FAIL: load ('%s')\n", frust_plugin_last_error());
        return 1;
    }

    auto run_it = (int64_t(*)(int64_t))frust_plugin_get_fn(h, "run_it");
    if (!run_it) {
        std::fprintf(stderr, "FAIL: get_fn(run_it) returned null\n");
        return 1;
    }

    int64_t result = run_it(10); // helper_double(10) + 1 = 21
    std::printf("run_it(10) = %lld\n", (long long)result);

    frust_plugin_unload(h);

    bool ok = (result == 21);
    std::printf("%s\n", ok ? "ALL_CHECKS_PASSED" : "CHECKS_FAILED");
    return ok ? 0 : 1;
}
