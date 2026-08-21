// frust_plugin_host - embed Frust in any C/C++ application, either as a
// live-reloadable plugin system or as a general hosted scripting
// language. Same underlying mechanism either way: this library JIT-
// compiles .frust source into the host process, with each load() call
// getting its own isolated LLVM ORC JITDylib - that isolation is what
// makes unload()/reload() real (tear down just that plugin's compiled
// code and symbols, not the whole process).
//
// Deliberately a plain C ABI, not a C++ class - so this is callable from
// any host toolchain (a different compiler/STL, C#/P-Invoke, Python via
// ctypes, ...), not just a C++ app built with the exact same compiler as
// this library. FrustPluginHandle is an opaque pointer; nothing about
// Frust's/LLVM's internal types leaks into this header.
//
// -----------------------------------------------------------------------
// Two directions of data flow, both supported:
// -----------------------------------------------------------------------
//
// 1. HOST calls INTO a loaded plugin - frust_plugin_get_fn() resolves a
//    `pub fn` (or plain top-level fn) by name to a raw function pointer.
//    The host already knows what signature to expect (whatever contract
//    it defines for its own plugins - this library doesn't enforce one),
//    so it casts the returned pointer itself, e.g.:
//
//      auto fn = (int64_t(*)(int64_t))frust_plugin_get_fn(h, "on_event");
//      int64_t result = fn(42);
//
// 2. Frust code calls OUT to a HOST-provided native function via
//    `extern fn` - two ways to make that resolve:
//    a) Free: if the host process already exports the symbol (dllexport
//       on Windows / default visibility on Linux), it resolves
//       automatically - the same DynamicLibrarySearchGenerator mechanism
//       frust_compiler's own JIT already uses for frust_print_str etc.
//       Nothing to call here for this case.
//    b) Explicit: frust_plugin_register_host_function() registers a
//       name -> function-pointer mapping directly, for cases where
//       relying on whole-process symbol export isn't practical (a
//       lambda, a class-method trampoline, a symbol you don't want
//       globally exported). Call this before load()ing any plugin that
//       needs it - the registry is process-wide and consulted for every
//       plugin's JITDylib.
// -----------------------------------------------------------------------

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32)
#define FRUST_PLUGIN_HOST_API __declspec(dllexport)
#else
#define FRUST_PLUGIN_HOST_API __attribute__((visibility("default")))
#endif

typedef struct FrustPluginHandleImpl* FrustPluginHandle;

// Parses, codegens, and JITs the .frust file at `path` into a fresh,
// isolated JITDylib. Returns NULL on any parse/codegen error (details go
// to stderr, matching frust_compiler's own error reporting - no separate
// error-string API in v1).
FRUST_PLUGIN_HOST_API FrustPluginHandle frust_plugin_load(const char* path);

// Tears down this plugin's JITDylib - frees its compiled code, removes
// its symbols. Any function pointers previously returned by
// frust_plugin_get_fn() for this handle become invalid; the host must
// not call them after this.
FRUST_PLUGIN_HOST_API void frust_plugin_unload(FrustPluginHandle handle);

// The actual hot-reload operation. Re-parses the source at `handle`'s
// path and compares its AST content hash (FRUST_LANG_SPEC.md 1.4)
// against what's already loaded:
//   - Unchanged: the SAME handle is returned untouched - no unload, no
//     recompile, no relink. Every function pointer already obtained from
//     `handle` stays valid. This is the zero-latency path, for the
//     common case of a reload trigger firing with no real edit behind it
//     (a no-op save, a duplicate file-watcher event).
//   - Changed (or the path can no longer be parsed cleanly - errors go
//     to stderr the normal way): `handle` is unloaded and the source is
//     re-parsed/re-JITed fresh into a NEW handle. NULL on failure (the
//     old handle is still torn down either way - a failed reload doesn't
//     leave the stale version running). Every function pointer obtained
//     from the OLD handle is invalid after this call, even on failure -
//     re-fetch from the new handle.
// Since which of these two happens isn't known until the content is
// compared, a caller that must tell them apart should compare the
// returned handle pointer against the one it passed in.
FRUST_PLUGIN_HOST_API FrustPluginHandle frust_plugin_reload(FrustPluginHandle handle);

// Resolves a top-level function by name within this plugin to a raw
// function pointer, or NULL if no such symbol exists. The host is
// responsible for casting it to the correct signature - this library has
// no way to know what one to expect.
FRUST_PLUGIN_HOST_API void* frust_plugin_get_fn(FrustPluginHandle handle, const char* name);

// Registers a host-provided native function under `name`, so any
// plugin's `extern fn <name>(...)` resolves to it even if the host
// process doesn't export that symbol globally. Process-wide (not
// per-plugin) - call before loading any plugin that needs it. Safe to
// call again with a new function pointer for the same name (last call
// wins for plugins loaded after that point; already-loaded plugins keep
// whatever was registered when they loaded).
FRUST_PLUGIN_HOST_API void frust_plugin_register_host_function(const char* name, void* fn_ptr);

// -----------------------------------------------------------------------
// Lifecycle convenience wrappers - a RECOMMENDED, NOT ENFORCED, naming
// convention for plugins that want one: `on_init() -> i64`,
// `on_event(id: i64, arg: i64) -> i64`, `on_unload() -> i64`. This
// library stays contract-agnostic at its core (frust_plugin_get_fn
// works with any function name a host chooses) - Frust has no
// interfaces/traits to enforce a contract at compile time, so nothing
// here requires a plugin to implement any of these. These three
// functions just save a host that DOES follow the convention from
// hand-rolling get_fn + a function-pointer cast every time.
//
// A plugin missing the corresponding function is not an error: these
// return 0 and do nothing, so an optional lifecycle hook a plugin
// doesn't implement is simply a no-op for the host, not a failure it
// has to handle specially.
// -----------------------------------------------------------------------
FRUST_PLUGIN_HOST_API int64_t frust_plugin_call_on_init(FrustPluginHandle handle);
FRUST_PLUGIN_HOST_API int64_t frust_plugin_call_on_event(FrustPluginHandle handle, int64_t id, int64_t arg);
// Convention only - does NOT call frust_plugin_unload() itself. Call
// this first (if you want the plugin's own cleanup to run), then
// frust_plugin_unload() separately.
FRUST_PLUGIN_HOST_API int64_t frust_plugin_call_on_unload(FrustPluginHandle handle);

#ifdef __cplusplus
}
#endif
