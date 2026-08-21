// Verification harness for the AST-content-hash reload skip
// (FRUST_LANG_SPEC.md 1.4) - see FrustPluginHost.cpp's frust_plugin_reload.
// Proves both halves of the contract: reloading with no real source
// change returns the SAME handle untouched (no unload/recompile), and
// reloading after a real change returns a genuinely new, working one.

#include "frust_plugin_host/FrustPluginHost.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

namespace {

std::string readFile(const std::string& path) {
    std::ifstream in(path);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

void writeFile(const std::string& path, const std::string& content) {
    std::ofstream out(path, std::ios::trunc);
    out << content;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: reload_example <v1.frust> <v2.frust> <scratch_work.frust>\n");
        return 1;
    }
    std::string v1Path = argv[1];
    std::string v2Path = argv[2];
    std::string workPath = argv[3];

    std::string v1Content = readFile(v1Path);
    std::string v2Content = readFile(v2Path);
    writeFile(workPath, v1Content);

    FrustPluginHandle h1 = frust_plugin_load(workPath.c_str());
    if (!h1) { std::fprintf(stderr, "FAIL: initial load\n"); return 1; }

    auto getValue = [](FrustPluginHandle h) -> int64_t {
        auto fn = (int64_t(*)())frust_plugin_get_fn(h, "get_value");
        return fn ? fn() : -1;
    };

    int64_t v1Result = getValue(h1); // expect 111

    // No edit to the file - reload should detect the AST hash is
    // unchanged and hand back the exact same handle.
    FrustPluginHandle h2 = frust_plugin_reload(h1);
    bool sameHandleOnNoop = (h2 == h1);
    int64_t v1ResultAfterNoopReload = getValue(h2); // still expect 111, same compiled code

    // Real edit - reload should now do the full unload/recompile/relink
    // and hand back a genuinely different handle.
    writeFile(workPath, v2Content);
    FrustPluginHandle h3 = frust_plugin_reload(h2);
    bool differentHandleOnRealChange = (h3 != h2) && (h3 != nullptr);
    int64_t v2Result = getValue(h3); // expect 222

    std::printf("v1=%lld sameHandleOnNoop=%d v1AfterNoopReload=%lld differentHandleOnRealChange=%d v2=%lld\n",
        (long long)v1Result, sameHandleOnNoop ? 1 : 0, (long long)v1ResultAfterNoopReload,
        differentHandleOnRealChange ? 1 : 0, (long long)v2Result);

    if (h3) frust_plugin_unload(h3);

    bool allOk = v1Result == 111 && sameHandleOnNoop && v1ResultAfterNoopReload == 111
        && differentHandleOnRealChange && v2Result == 222;

    std::printf("%s\n", allOk ? "ALL_CHECKS_PASSED" : "CHECKS_FAILED");
    return allOk ? 0 : 1;
}
