#pragma once

// C++ interface for loading a plugin from source text with the full set of
// providers: sibling files for `use self::x;` AND pods for `use pod;` /
// `import pod, "v";`. Nothing here opens a file. (FrustPluginHost.h's
// frust_plugin_load_source is the plain-C form and supports sibling files only.)
//
// request.sources[0] is the plugin's own source; its `name` labels the plugin
// in diagnostics and identifies it for reload.

#include "frust_plugin_host/FrustPluginHost.h"

#include <CompilerApi.h>
#include <EnvironmentCompile.h>

namespace frust_plugin_host {

FRUST_PLUGIN_HOST_API FrustPluginHandle loadFromSource(const frust::CompileRequest& request);

// Same contract as frust_plugin_reload: an unchanged program returns the same
// handle; a changed one is unloaded and loaded again (on_init is called).
FRUST_PLUGIN_HOST_API FrustPluginHandle reloadFromSource(FrustPluginHandle handle, const frust::CompileRequest& request);

// Loads the plugin whose source is `entryPath` in the environment, reading it, the files its
// `use self::x;` lines name, and the pods it uses (under `podsRoot`) all through `env`.
// Nothing here opens a file. See EnvironmentCompile.h for the pods layout.
FRUST_PLUGIN_HOST_API FrustPluginHandle loadFromEnvironment(frust::HostEnvironment& env,
                                                            const frust::FileCompileRequest& request);

} // namespace frust_plugin_host
