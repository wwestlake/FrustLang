// Verification harness for Phase 2: plugin manifest + lifecycle
// convenience wrappers. See lifecycle_plugin.frust - its manifest is now
// embedded directly in the source (`manifest "...";`) rather than a
// separate companion .json, so this test reads it via
// frust_plugin_get_manifest() on the already-loaded handle - the SAME
// manifest frust_plugin_load() itself already parsed and verified, not
// a second, independently-read copy that could drift out of sync.

#include "frust_plugin_host/FrustPluginHost.h"

#include <cstdio>
#include <cstring>
#include <string>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: lifecycle_example <plugin.frust>\n");
        return 1;
    }
    std::string pluginPath = argv[1];

    FrustPluginHandle plugin = frust_plugin_load(pluginPath.c_str());
    if (!plugin) { std::fprintf(stderr, "FAIL: plugin load ('%s')\n", frust_plugin_last_error()); return 1; }

    FrustPluginManifestHandle manifest = frust_plugin_get_manifest(plugin);
    if (!manifest) { std::fprintf(stderr, "FAIL: get_manifest\n"); return 1; }

    bool manifestOk =
        std::strcmp(frust_plugin_manifest_name(manifest), "lifecycle_plugin") == 0 &&
        std::strcmp(frust_plugin_manifest_version(manifest), "0.1.0") == 0 &&
        frust_plugin_manifest_entry_point_count(manifest) == 3 &&
        std::strcmp(frust_plugin_manifest_entry_point(manifest, 0), "on_init") == 0 &&
        std::strcmp(frust_plugin_manifest_entry_point(manifest, 1), "on_event") == 0 &&
        std::strcmp(frust_plugin_manifest_entry_point(manifest, 2), "on_unload") == 0 &&
        frust_plugin_manifest_entry_point(manifest, 3) == nullptr; // out of range -> NULL

    std::printf("manifest: name=%s version=%s entryCount=%d ok=%d\n",
        frust_plugin_manifest_name(manifest),
        frust_plugin_manifest_version(manifest),
        frust_plugin_manifest_entry_point_count(manifest),
        manifestOk ? 1 : 0);

    frust_plugin_manifest_free(manifest);

    int64_t initResult = frust_plugin_call_on_init(plugin);       // expect 111
    int64_t eventResult = frust_plugin_call_on_event(plugin, 10, 32); // expect 42
    int64_t unloadResult = frust_plugin_call_on_unload(plugin);   // expect 222

    // A NULL handle should no-op to 0, not crash - proves this convention
    // is genuinely optional, not silently required.
    int64_t missingResult = frust_plugin_call_on_event(nullptr, 1, 1);

    std::printf("on_init=%lld on_event=%lld on_unload=%lld missing(null handle)=%lld\n",
        (long long)initResult, (long long)eventResult, (long long)unloadResult, (long long)missingResult);

    frust_plugin_unload(plugin);

    bool lifecycleOk = initResult == 111 && eventResult == 42 && unloadResult == 222 && missingResult == 0;
    bool allOk = manifestOk && lifecycleOk;

    std::printf("%s\n", allOk ? "ALL_CHECKS_PASSED" : "CHECKS_FAILED");
    return allOk ? 0 : 1;
}
