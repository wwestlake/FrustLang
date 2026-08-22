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

// One `impl InterfaceName for TypeName` this plugin provides, plus the
// free-function constructor a host calls to get an instance (see
// automation.fr's new_ramp/new_decay - `own`-heap-allocating, so the
// returned pointer survives past the constructor call). Lets a host
// discover "this plugin gives me an Automation" and know how to
// actually obtain one, without hardcoding the concrete type name or
// constructor ahead of time. Does NOT (yet) list the interface's own
// method names - the host is still expected to already know an
// interface's shape (e.g. "Automation has tick(f32) -> f32") the same
// way it already needs to know an `entryPoint`'s signature; full
// method-signature reflection is future work, not this pass.
struct PluginInterfaceImpl {
    std::string interfaceName;
    std::string typeName;
    std::string constructorFn;
};

// One host-provided function this plugin expects to exist (usually via
// frust_plugin_register_host_function) before it will work correctly -
// lets a host check compatibility BEFORE loading, rather than
// discovering a missing symbol only when the JIT module link fails.
struct RequiredHostFunction {
    std::string name;
    std::string description;
};

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

    // The actual loadable .frust source this manifest describes, as a
    // path relative to the manifest file's own directory - so a host
    // can go from "found a plugin.json" straight to
    // frust_plugin_load() without a separate, out-of-band convention
    // for finding the source.
    std::string sourceFile;

    // Interfaces (FRUST_LANG_SPEC.md section 4.1) this plugin
    // implements - see PluginInterfaceImpl above.
    std::vector<PluginInterfaceImpl> implementedInterfaces;

    // Event names (docs/17-Plugin-Automation-Layer.md) this plugin
    // fires and/or registers a handler for during its own on_init.
    // Discoverable ahead of load, same motivation as
    // implementedInterfaces - a host shouldn't need to already know a
    // plugin's internals to know what events it cares about.
    std::vector<std::string> firedEvents;
    std::vector<std::string> listenedEvents;

    // Host functions this plugin's extern fn declarations expect to
    // resolve - see RequiredHostFunction above.
    std::vector<RequiredHostFunction> requiredHostFunctions;
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

// The plugin's actual loadable .frust source, relative to the manifest
// file's own directory. Empty string if the manifest didn't declare one.
FRUST_PLUGIN_HOST_API const char* frust_plugin_manifest_source_file(FrustPluginManifestHandle handle);

// Interfaces this plugin implements - see PluginInterfaceImpl.
FRUST_PLUGIN_HOST_API int32_t frust_plugin_manifest_interface_count(FrustPluginManifestHandle handle);
FRUST_PLUGIN_HOST_API const char* frust_plugin_manifest_interface_name(FrustPluginManifestHandle handle, int32_t index);
FRUST_PLUGIN_HOST_API const char* frust_plugin_manifest_interface_type(FrustPluginManifestHandle handle, int32_t index);
FRUST_PLUGIN_HOST_API const char* frust_plugin_manifest_interface_constructor(FrustPluginManifestHandle handle, int32_t index);

// Event names this plugin fires / listens for - docs/17-Plugin-Automation-Layer.md.
FRUST_PLUGIN_HOST_API int32_t frust_plugin_manifest_fired_event_count(FrustPluginManifestHandle handle);
FRUST_PLUGIN_HOST_API const char* frust_plugin_manifest_fired_event(FrustPluginManifestHandle handle, int32_t index);
FRUST_PLUGIN_HOST_API int32_t frust_plugin_manifest_listened_event_count(FrustPluginManifestHandle handle);
FRUST_PLUGIN_HOST_API const char* frust_plugin_manifest_listened_event(FrustPluginManifestHandle handle, int32_t index);

// Host functions this plugin expects to be registered (usually via
// frust_plugin_register_host_function) before it will work correctly -
// lets a host check compatibility before loading.
FRUST_PLUGIN_HOST_API int32_t frust_plugin_manifest_required_host_function_count(FrustPluginManifestHandle handle);
FRUST_PLUGIN_HOST_API const char* frust_plugin_manifest_required_host_function_name(FrustPluginManifestHandle handle, int32_t index);
FRUST_PLUGIN_HOST_API const char* frust_plugin_manifest_required_host_function_description(FrustPluginManifestHandle handle, int32_t index);

#ifdef __cplusplus
}
#endif
