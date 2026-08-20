# frust_plugin_host

Embed Frust in any C/C++ application - as a live-reloadable plugin
system, or just as a general hosted scripting language. See
[`include/frust_plugin_host/FrustPluginHost.h`](include/frust_plugin_host/FrustPluginHost.h)
for the full design writeup; this is the short version.

Plain C ABI (`extern "C"`), not a C++ class - callable from any host
toolchain, not just one built with the exact same compiler as this
library.

```c
FrustPluginHandle h = frust_plugin_load("plugin.frust");
auto fn = (int64_t(*)(int64_t))frust_plugin_get_fn(h, "plugin_double");
int64_t result = fn(21);
frust_plugin_unload(h);
```

Each `frust_plugin_load()` gets its own isolated LLVM ORC `JITDylib` -
that isolation is what makes `frust_plugin_unload()`/
`frust_plugin_reload()` real: tearing one plugin down doesn't touch the
host process or any other loaded plugin.

Two directions of calling, both supported:

1. **Host calls into a plugin** - `frust_plugin_get_fn()`.
2. **Plugin calls out to the host** via `extern fn` - either for free (if
   the host process already exports the symbol, e.g. `dllexport`), or
   explicitly via `frust_plugin_register_host_function()` for symbols
   that aren't/can't be globally exported.

See `examples/host_example.cpp` for a complete, verified round trip:
host-provided function called from a plugin, plugin function called from
the host, unload, and a genuine hot-reload (the plugin source changes on
disk between calls, and the new behavior is what actually runs).

## Manifest

A `plugin.json` sitting next to the `.frust` source, modeled directly on
`frate`'s existing pod-metadata format:

```json
{
    "name": "my_plugin",
    "version": "0.1.0",
    "description": "...",
    "entryPoints": ["on_init", "on_event", "on_unload"],
    "requiredHostApiVersion": "0.1.0"
}
```

Read via `frust_plugin_manifest_load()`
([`FrustPluginManifest.h`](include/frust_plugin_host/FrustPluginManifest.h)) -
purely metadata about a plugin (name/version/what it claims to export),
independent of actually loading/running it.

## Lifecycle convention (recommended, not enforced)

`frust_plugin_call_on_init(handle)` / `frust_plugin_call_on_event(handle,
id, arg)` / `frust_plugin_call_on_unload(handle)` - thin convenience
wrappers around `frust_plugin_get_fn()` for a plugin that implements
`on_init() -> i64` / `on_event(id: i64, arg: i64) -> i64` /
`on_unload() -> i64`. A plugin missing one of these is not an error -
the wrapper no-ops to `0` rather than requiring every plugin to
implement every hook. See `examples/lifecycle_example.cpp` +
`lifecycle_plugin.frust`/`.json` for a full verified round trip.

## Status

v1. `.frust` plugin files are single-file only (no `use self::` module
splitting yet - each plugin is one source file). No contract/lifecycle is
*enforced* by this library (the lifecycle wrappers above are optional
convenience, not a requirement) - Frust has no interfaces/traits to
enforce one at compile time anyway. Plugin discovery (scanning a
directory for available plugins/manifests) isn't implemented yet - real
v2 work once there's an actual multi-plugin host to build it against.
