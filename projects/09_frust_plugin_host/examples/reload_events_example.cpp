// Verification harness for two of this session's completion-plan
// fixes:
//
// 1. frust_plugin_reload() must re-run on_init() on the new handle, so
//    event/service registrations survive a real (content-changed)
//    reload instead of silently vanishing - this was a confirmed real
//    bug (frust_plugin_unload correctly purged the OLD registrations,
//    but nothing ever re-created them on the new handle).
// 2. frust_plugin_last_error() must return a real, non-empty message
//    after a failed load - the only way a GUI host with no visible
//    console (confirmed: the JUCE IDE is built via juce_add_gui_app)
//    can show the user anything went wrong at all.

#include "frust_plugin_host/FrustPluginHost.h"

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

namespace {
int64_t g_fireCount = 0;

extern "C" int64_t test_mark_fired(int64_t id) {
    g_fireCount += id;
    return g_fireCount;
}

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
    if (argc < 3) {
        std::fprintf(stderr, "usage: reload_events_example <event_plugin.frust> <event_plugin_v2.frust>\n");
        return 1;
    }
    std::string v1Content = readFile(argv[1]);
    std::string v2Content = readFile(argv[2]);
    std::string workPath = std::string(argv[1]) + ".reload_work.frust";
    writeFile(workPath, v1Content);

    frust_plugin_register_host_function("test_mark_fired", (void*)&test_mark_fired);

    // --- Part 1: frust_plugin_last_error() after a genuine failure ---
    FrustPluginHandle bad = frust_plugin_load("this_file_does_not_exist.frust");
    bool loadFailedAsExpected = (bad == nullptr);
    std::string lastErr = frust_plugin_last_error();
    bool lastErrorPopulated = !lastErr.empty();
    std::printf("last-error check: loadFailed=%d lastError='%s'\n", loadFailedAsExpected, lastErr.c_str());

    // --- Part 2: reload must restore event registrations ---
    FrustPluginHandle h = frust_plugin_load(workPath.c_str());
    if (!h) { std::fprintf(stderr, "FAIL: initial load\n"); return 1; }

    int64_t initResult = frust_plugin_call_on_init(h); // registers the handler
    bool initOk = initResult == 111;

    frust_fire_event("something_happened", nullptr);
    bool firstFireOk = (g_fireCount == 1);

    // Force a REAL (content-changed) reload.
    writeFile(workPath, v2Content);
    FrustPluginHandle h2 = frust_plugin_reload(h);
    bool reloadedOk = (h2 != nullptr && h2 != h);

    // The actual proof: fire the event WITHOUT calling on_init again -
    // if reload() didn't re-register the handler, this does nothing.
    frust_fire_event("something_happened", nullptr);
    bool fireAfterReloadOk = (g_fireCount == 2);

    frust_plugin_unload(h2);

    std::printf("initOk=%d firstFireOk=%d reloadedOk=%d fireAfterReloadOk=%d (count=%lld)\n",
        initOk, firstFireOk, reloadedOk, fireAfterReloadOk, (long long)g_fireCount);

    bool allOk = loadFailedAsExpected && lastErrorPopulated && initOk && firstFireOk && reloadedOk && fireAfterReloadOk;
    std::printf("%s\n", allOk ? "ALL_CHECKS_PASSED" : "CHECKS_FAILED");
    return allOk ? 0 : 1;
}
