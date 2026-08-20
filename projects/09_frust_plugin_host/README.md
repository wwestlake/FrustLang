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

## Status

v1. `.frust` plugin files are single-file only (no `use self::` module
splitting yet - each plugin is one source file). No contract/lifecycle is
enforced by this library (no required `on_init`/`on_unload` functions) -
Frust has no interfaces/traits to enforce one at compile time anyway, so
each host application defines its own convention for what its plugins
must export.
