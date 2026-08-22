// Verification harness for the event subscription system
// (docs/17-Plugin-Automation-Layer.md): a plugin registers a handler
// for a named event during its own on_init(), the host fires that
// event and confirms the handler actually ran, and - the real safety
// property - after the plugin is unloaded, firing the SAME event again
// must not crash and must not call the (now-invalid) handler, proving
// frust_plugin_unload's automatic cleanup actually works.
//
// Also verifies the manifest (event_plugin.json) actually describes
// this plugin correctly - declared listenedEvents/requiredHostFunctions
// match what the plugin's source really does - and uses the manifest
// to check host compatibility BEFORE loading, the real "connect it up
// properly" use case: a host can refuse to even attempt loading a
// plugin whose required host functions it doesn't provide, instead of
// discovering that only when the JIT module link fails.

#include "frust_plugin_host/FrustPluginHost.h"
#include "frust_plugin_host/FrustPluginManifest.h"

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
    if (argc < 3) {
        std::fprintf(stderr, "usage: event_example <event_plugin.frust> <event_plugin.json>\n");
        return 1;
    }
    std::string pluginPath = argv[1];
    std::string manifestPath = argv[2];

    // --- Manifest round-trip + pre-load compatibility check ---
    FrustPluginManifestHandle manifest = frust_plugin_manifest_load(manifestPath.c_str());
    if (!manifest) { std::fprintf(stderr, "FAIL: manifest load\n"); return 1; }

    bool manifestSourceOk = std::strcmp(frust_plugin_manifest_source_file(manifest), "event_plugin.frust") == 0;

    bool manifestListenedOk = frust_plugin_manifest_listened_event_count(manifest) == 1
        && std::strcmp(frust_plugin_manifest_listened_event(manifest, 0), "something_happened") == 0;

    bool manifestRequiredFnOk = frust_plugin_manifest_required_host_function_count(manifest) == 1
        && std::strcmp(frust_plugin_manifest_required_host_function_name(manifest, 0), "test_mark_fired") == 0;

    // The actual compatibility check a real host would do: for every
    // function this manifest says the plugin needs, do we (the host)
    // provide it? Here we know we will (test_mark_fired is registered
    // right below) - a real host would check this BEFORE registering/
    // loading and refuse to proceed on a mismatch.
    bool weProvideEverythingRequired = true;
    for (int32_t i = 0; i < frust_plugin_manifest_required_host_function_count(manifest); ++i) {
        std::string needed = frust_plugin_manifest_required_host_function_name(manifest, i);
        if (needed != "test_mark_fired") weProvideEverythingRequired = false; // the only one this harness provides
    }

    frust_plugin_manifest_free(manifest);

    // --- Real load + event flow, same as before ---
    frust_plugin_register_host_function("test_mark_fired", (void*)&test_mark_fired);

    FrustPluginHandle h = frust_plugin_load(pluginPath.c_str());
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

    std::printf("manifest: source=%d listened=%d requiredFn=%d compatible=%d\n",
        manifestSourceOk, manifestListenedOk, manifestRequiredFnOk, weProvideEverythingRequired);
    std::printf("init=%lld fireCount after: before=%d first=%d second=%d unknown=%d afterUnload=%d (count=%lld)\n",
        (long long)initResult, beforeFireOk, firstFireOk, secondFireOk, unknownEventOk, afterUnloadOk, (long long)g_fireCount);

    bool allOk = manifestSourceOk && manifestListenedOk && manifestRequiredFnOk && weProvideEverythingRequired
        && initOk && beforeFireOk && firstFireOk && secondFireOk && unknownEventOk && afterUnloadOk;
    std::printf("%s\n", allOk ? "ALL_CHECKS_PASSED" : "CHECKS_FAILED");
    return allOk ? 0 : 1;
}
