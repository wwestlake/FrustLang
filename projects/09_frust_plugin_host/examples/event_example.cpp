// Verification harness for the event subscription system
// (docs/17-Plugin-Automation-Layer.md): a plugin registers a handler
// for a named event during its own on_init(), the host fires that
// event and confirms the handler actually ran, and - the real safety
// property - after the plugin is unloaded, firing the SAME event again
// must not crash and must not call the (now-invalid) handler, proving
// frust_plugin_unload's automatic cleanup actually works.
//
// Also verifies the plugin's own embedded manifest (`manifest "...";`
// inside event_plugin.frust, not a separate companion file) actually
// describes it correctly - declared listenedEvents/requiredHostFunctions
// match what the plugin's source really does - read via
// frust_plugin_get_manifest() on the already-loaded handle, i.e. the
// SAME manifest frust_plugin_load() itself already used to gate the
// load ("no manifest, no load"), not a second independently-parsed
// copy. Since the manifest declares test_mark_fired as a
// requiredHostFunctions entry, it must be registered BEFORE
// frust_plugin_load() is even called, or the load itself would be
// refused - this is the real "connect it up properly" use case in
// practice, not just an optional pre-check a host can skip.

#include "frust_plugin_host/FrustPluginHost.h"

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>

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
    std::string pluginPath = argv[1];

    frust_plugin_register_host_function("test_mark_fired", (void*)&test_mark_fired);

    FrustPluginHandle h = frust_plugin_load(pluginPath.c_str());
    if (!h) { std::fprintf(stderr, "FAIL: load ('%s')\n", frust_plugin_last_error()); return 1; }

    // --- Manifest round-trip, read from the loaded handle itself ---
    FrustPluginManifestHandle manifest = frust_plugin_get_manifest(h);
    if (!manifest) { std::fprintf(stderr, "FAIL: get_manifest\n"); return 1; }

    bool manifestListenedOk = frust_plugin_manifest_listened_event_count(manifest) == 1
        && std::strcmp(frust_plugin_manifest_listened_event(manifest, 0), "something_happened") == 0;

    bool manifestRequiredFnOk = frust_plugin_manifest_required_host_function_count(manifest) == 1
        && std::strcmp(frust_plugin_manifest_required_host_function_name(manifest, 0), "test_mark_fired") == 0;

    frust_plugin_manifest_free(manifest);

    // --- Real event flow, same as before ---
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

    std::printf("manifest: listened=%d requiredFn=%d\n", manifestListenedOk, manifestRequiredFnOk);
    std::printf("init=%lld fireCount after: before=%d first=%d second=%d unknown=%d afterUnload=%d (count=%lld)\n",
        (long long)initResult, beforeFireOk, firstFireOk, secondFireOk, unknownEventOk, afterUnloadOk, (long long)g_fireCount);

    bool allOk = manifestListenedOk && manifestRequiredFnOk
        && initOk && beforeFireOk && firstFireOk && secondFireOk && unknownEventOk && afterUnloadOk;
    std::printf("%s\n", allOk ? "ALL_CHECKS_PASSED" : "CHECKS_FAILED");
    return allOk ? 0 : 1;
}
