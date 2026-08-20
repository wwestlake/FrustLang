// frust_plugin_host manifest - name/version/description/entry-point
// metadata for a plugin, read from a `plugin.json` sitting next to its
// `.frust` source. Modeled directly on frate's existing PodMetadata/
// PodMetadataJson pattern (projects/05_frate/include/frate/
// PodMetadata.h, src/PodMetadataJson.cpp) - same struct-plus-JSON-
// round-trip shape, not a new format invented for this.
//
// This is metadata ABOUT a plugin (what it's called, what it claims to
// export) - it does not load or run anything. Load the plugin itself
// separately via frust_plugin_load() (FrustPluginHost.h); nothing here
// requires that to have happened first, or ties the two together.

#pragma once

#include <stdint.h>

#ifdef __cplusplus

#include <optional>
#include <string>
#include <vector>

namespace frust_plugin_host {

struct PluginManifest {
    std::string name;
    std::string version;
    std::string description;
    // The function names this plugin claims to export - informational
    // (a host can display/validate against it), not enforced by this
    // library. Typically includes whatever lifecycle functions the
    // plugin implements (see FrustPluginHost.h's lifecycle convenience
    // wrappers for the recommended, non-enforced convention).
    std::vector<std::string> entryPoints;
    // The lowest frust_plugin_host API version this plugin expects -
    // informational for now (no actual version-gating logic exists
    // yet); a real host wanting compatibility checking reads this and
    // decides for itself.
    std::string requiredHostApiVersion;
};

// Parses a plugin.json file. Returns std::nullopt on any read/parse
// failure (details go to stderr, matching this library's existing
// error-reporting style).
std::optional<PluginManifest> LoadPluginManifest(const std::string& path);

} // namespace frust_plugin_host

extern "C" {
#endif

#if defined(_WIN32)
#define FRUST_PLUGIN_HOST_API __declspec(dllexport)
#else
#define FRUST_PLUGIN_HOST_API __attribute__((visibility("default")))
#endif

typedef struct FrustPluginManifestHandleImpl* FrustPluginManifestHandle;

// C-ABI surface over the same PluginManifest, for non-C++ hosts. NULL
// on any read/parse failure.
FRUST_PLUGIN_HOST_API FrustPluginManifestHandle frust_plugin_manifest_load(const char* path);
FRUST_PLUGIN_HOST_API void frust_plugin_manifest_free(FrustPluginManifestHandle handle);

FRUST_PLUGIN_HOST_API const char* frust_plugin_manifest_name(FrustPluginManifestHandle handle);
FRUST_PLUGIN_HOST_API const char* frust_plugin_manifest_version(FrustPluginManifestHandle handle);
FRUST_PLUGIN_HOST_API const char* frust_plugin_manifest_description(FrustPluginManifestHandle handle);
FRUST_PLUGIN_HOST_API int32_t frust_plugin_manifest_entry_point_count(FrustPluginManifestHandle handle);
// index out of range returns NULL rather than crashing.
FRUST_PLUGIN_HOST_API const char* frust_plugin_manifest_entry_point(FrustPluginManifestHandle handle, int32_t index);

#ifdef __cplusplus
}
#endif
