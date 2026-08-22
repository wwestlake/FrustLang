// frust_plugin_host manifest - name/version/description/entry-point
// metadata for a plugin. Originally read from a `plugin.json` sitting
// next to its `.frust` source (modeled on frate's PodMetadata/
// PodMetadataJson pattern - projects/05_frate/include/frate/
// PodMetadata.h, src/PodMetadataJson.cpp); a plugin's REAL manifest now
// lives embedded directly in its own source (`manifest "...";`, a
// top-level Frust declaration - see AST.h's ManifestDecl and
// Codegen.h's compileManifestDecl) - frust_plugin_load() (
// FrustPluginHost.h) reads it straight off the compiled module and
// enforces "no manifest, no load." LoadPluginManifest()/
// frust_plugin_manifest_load() (file-based) and ParseManifestJson()
// (raw-JSON-text-based, what frust_plugin_load actually calls) both
// remain - useful for a manifest a host wants to inspect independently
// of loading the plugin's code (see app_identity_example.cpp) - but
// they are no longer how frust_plugin_load itself decides whether to
// load. frust_plugin_get_manifest() (FrustPluginHost.h) returns the
// SAME manifest a loaded plugin's handle already carries, not a fresh
// re-read from anywhere.
//
// This is metadata ABOUT a plugin (what it's called, what it claims to
// export) - it does not load or run anything itself. PluginManifest/
// IsCompatible() below have no dependency on frust_plugin_load having
// been called first.

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

    // Which application(s) this plugin is built for - identity strings
    // ("lagdaemon-ide", whatever a host calls itself via
    // frust_plugin_host_set_application_identity in FrustPluginHost.h).
    // Empty means "not restricted" - compatible with any host, same
    // permissive-when-absent stance as every other optional manifest
    // field. Non-empty means the CURRENT host's own declared identity
    // must appear in this list for frust_plugin_manifest_is_compatible()
    // to consider it a match.
    std::vector<std::string> intendedApplications;
};

// Parses a plugin.json file. Returns std::nullopt on any read/parse
// failure (details go to stderr, matching this library's existing
// error-reporting style).
std::optional<PluginManifest> LoadPluginManifest(const std::string& path);

// The actual JSON-parsing logic LoadPluginManifest uses, factored out so
// FrustPluginHost.cpp can parse a manifest's raw JSON text pulled
// directly out of a compiled plugin's own `manifest "...";` declaration
// (see AST.h's kFrustPluginManifestGlobalName) - not just from a file on
// disk. `contextLabel` is only used in error messages (e.g. the plugin's
// source path) so a parse failure is traceable back to which plugin it
// came from. Returns std::nullopt on any parse failure (reported to
// stderr, same as LoadPluginManifest).
std::optional<PluginManifest> ParseManifestJson(const std::string& json, const std::string& contextLabel);

// The real compatibility check - built into the library so a host
// doesn't have to hand-roll it every time (previously the manifest was
// purely advisory: a host could READ requiredHostFunctions but nothing
// ever actually checked it). Checks, in order:
//   1. If `m.intendedApplications` is non-empty, the current host's own
//      declared identity (frust_plugin_host_set_application_identity,
//      FrustPluginHost.h) must appear in it. A host that never declared
//      an identity is treated as NOT matching a manifest that DOES
//      restrict to specific applications (conservative - silence on
//      the host side plus an explicit restriction on the plugin side
//      is exactly the "might be the wrong app" case worth catching).
//   2. Every name in `m.requiredHostFunctions` must currently be
//      resolvable (frust_plugin_is_host_function_available,
//      FrustPluginHost.h) - a real symbol check, not just "was it in
//      the manifest."
// Returns true only if both hold. On false, call
// LastIncompatibilityReason() for why.
bool IsCompatible(const PluginManifest& m);
const std::string& LastIncompatibilityReason();

} // namespace frust_plugin_host

extern "C" {
#endif

#if defined(_WIN32)
#define FRUST_PLUGIN_HOST_API __declspec(dllexport)
#else
#define FRUST_PLUGIN_HOST_API __attribute__((visibility("default")))
#endif

typedef struct FrustPluginManifestHandleImpl* FrustPluginManifestHandle;

#ifdef __cplusplus
}

namespace frust_plugin_host {
// Wraps an already-parsed PluginManifest (e.g. one FrustPluginHost.cpp
// extracted from a plugin's embedded `manifest "...";` declaration) as a
// FrustPluginManifestHandle the C ABI can hand back to a caller -
// FrustPluginHandleImpl and FrustPluginManifestHandleImpl are each
// private to their own .cpp file, so this is the one shared seam that
// lets frust_plugin_get_manifest() (FrustPluginHost.h) build a real
// handle without either file reaching into the other's internals. Takes
// ownership of a COPY - the caller's own PluginManifest is untouched.
FrustPluginManifestHandle WrapManifest(PluginManifest manifest);
} // namespace frust_plugin_host

extern "C" {
#endif

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

// Applications this plugin declares itself built for - see
// PluginManifest::intendedApplications.
FRUST_PLUGIN_HOST_API int32_t frust_plugin_manifest_intended_application_count(FrustPluginManifestHandle handle);
FRUST_PLUGIN_HOST_API const char* frust_plugin_manifest_intended_application(FrustPluginManifestHandle handle, int32_t index);

// The real compatibility check - see IsCompatible's doc above. 1 if
// compatible, 0 if not; call frust_plugin_manifest_incompatibility_reason()
// on 0 for why.
FRUST_PLUGIN_HOST_API int32_t frust_plugin_manifest_is_compatible(FrustPluginManifestHandle handle);
FRUST_PLUGIN_HOST_API const char* frust_plugin_manifest_incompatibility_reason(void);

#ifdef __cplusplus
}
#endif
