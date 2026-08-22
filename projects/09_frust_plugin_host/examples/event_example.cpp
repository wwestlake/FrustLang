// Verification harness for the event subscription system
// (docs/17-Plugin-Automation-Layer.md): a plugin registers a handler
// for a named event during its own on_init(), the host fires that
// event and confirms the handler actually ran, and - the real safety
// property - after the plugin is unloaded, firing the SAME event again
// must not crash and must not call the (now-invalid) handler, proving
// frust_plugin_unload's automatic cleanup actually works.

#include "frust_plugin_host/FrustPluginHost.h"

#include <cstdio>
#include <cstdint>

namespace {
int64_t g_fireCount = 0;

extern "C" int64_t test_mark_fired(int64_t id) {
    g_fireCount += id;
    return g_fireCount;
}
} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: event_example <event_plugin.frust>\n");
        return 1;
    }

    frust_plugin_register_host_function("test_mark_fired", (void*)&test_mark_fired);

    FrustPluginHandle h = frust_plugin_load(argv[1]);
    if (!h) { std::fprintf(stderr, "FAIL: load\n"); return 1; }

    int64_t initResult = frust_plugin_call_on_init(h); // registers the handler
    bool initOk = initResult == 111;

    // Handler not fired yet - only registered so far.
    bool beforeFireOk = (g_fireCount == 0);

    frust_fire_event("something_happened", nullptr);
    bool firstFireOk = (g_fireCount == 1);

    frust_fire_event("something_happened", nullptr);
    bool secondFireOk = (g_fireCount == 2);

    // Firing an event nobody subscribed to must be a harmless no-op.
    frust_fire_event("nobody_is_listening", nullptr);
    bool unknownEventOk = (g_fireCount == 2);

    frust_plugin_unload(h);

    // The real safety test: the handler pointed into now-unloaded JIT
    // code. If cleanup-on-unload didn't work, this either crashes
    // outright or silently calls into freed memory - either way,
    // g_fireCount must NOT change.
    frust_fire_event("something_happened", nullptr);
    bool afterUnloadOk = (g_fireCount == 2);

    std::printf("init=%lld fireCount after: before=%d first=%d second=%d unknown=%d afterUnload=%d (count=%lld)\n",
        (long long)initResult, beforeFireOk, firstFireOk, secondFireOk, unknownEventOk, afterUnloadOk, (long long)g_fireCount);

    bool allOk = initOk && beforeFireOk && firstFireOk && secondFireOk && unknownEventOk && afterUnloadOk;
    std::printf("%s\n", allOk ? "ALL_CHECKS_PASSED" : "CHECKS_FAILED");
    return allOk ? 0 : 1;
}
