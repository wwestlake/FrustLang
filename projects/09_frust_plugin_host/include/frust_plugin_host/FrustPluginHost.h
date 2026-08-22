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

// The message from the most recent failed operation on THIS THREAD
// (load/unload/reload/register_host_function) - "" if the last one on
// this thread succeeded, or none has run yet. Every failure still also
// goes to stderr as before (unchanged for anyone already relying on
// that), but stderr is invisible in a windowed GUI host with no console
// - this exists so a host like that can show the user *something*
// instead of a silent no-op. thread_local so concurrent load attempts
// on different threads don't clobber each other's error.
FRUST_PLUGIN_HOST_API const char* frust_plugin_last_error(void);

// Parses, codegens, and JITs the .frust file at `path` into a fresh,
// isolated JITDylib. Returns NULL on any parse/codegen error (details go
// to stderr AND frust_plugin_last_error(), matching frust_compiler's own
// error reporting style for the stderr half).
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
//     re-parsed/re-JITed fresh into a NEW handle, and - unlike the very
//     first frust_plugin_load() - its on_init() IS automatically called
//     if it has one, so event/service registrations a plugin made
//     during on_init survive a real reload instead of silently
//     vanishing (this was a real bug: unload() correctly purged the old
//     registrations, but nothing ever re-created them on the new
//     handle). NULL on failure (the old handle is still torn down
//     either way - a failed reload doesn't leave the stale version
//     running). Every function pointer obtained from the OLD handle is
//     invalid after this call, even on failure - re-fetch from the new
//     handle.
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

// Is a host function named `name` currently resolvable - i.e. would a
// plugin's `extern fn <name>(...)` actually link successfully right
// now? Checks the real JIT symbol table (both explicit
// frust_plugin_register_host_function() registrations and anything the
// host process itself exports), not just "was this name registered at
// some point" - a real answer, not a guess. Lets a host check "do I
// actually provide what this plugin needs" BEFORE loading, instead of
// discovering a missing symbol only when the JIT module link fails.
FRUST_PLUGIN_HOST_API int32_t frust_plugin_is_host_function_available(const char* name);

// -----------------------------------------------------------------------
// Application identity - lets a host declare which application it IS
// (once, at startup), so a plugin's manifest can declare which
// application(s) it's built FOR, and frust_plugin_manifest_is_compatible()
// (FrustPluginManifest.h) can give a real yes/no instead of a host
// having to hand-roll that comparison itself. Purely informational
// strings this library doesn't interpret beyond equality-checking them
// - "lagdaemon-ide", "my-other-app", whatever a host wants to call
// itself. Not set by default - a manifest with no declared target
// application is treated as compatible with any host (see
// frust_plugin_manifest_is_compatible's own doc for the exact rule).
// -----------------------------------------------------------------------
FRUST_PLUGIN_HOST_API void frust_plugin_host_set_application_identity(const char* appId);
// "" if never set.
FRUST_PLUGIN_HOST_API const char* frust_plugin_host_application_identity(void);

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

// -----------------------------------------------------------------------
// Event subscription - docs/17-Plugin-Automation-Layer.md. The inverse
// direction of frust_plugin_register_host_function(): there, the HOST
// gives a PLUGIN something to call; here, a PLUGIN gives the HOST
// something to call, keyed by an event name rather than a symbol name
// the caller has to already know. Domain-agnostic by construction - an
// event is a name plus an opaque payload whose meaning is defined
// entirely by whoever fires it and whoever handles it. This library
// never inspects, types, or constrains the payload.
// -----------------------------------------------------------------------

// Fires `name` with `payload` - every handler currently registered for
// that exact name (across every loaded plugin) is called, in
// registration order, with `payload` passed through unchanged.
FRUST_PLUGIN_HOST_API void frust_fire_event(const char* name, void* payload);

// Registers `handler` to run whenever `name` is fired. Must be called
// from WITHIN a plugin's own frust_plugin_call_on_init() OR
// frust_plugin_call_on_event() call (i.e. from that plugin's `on_init`
// or `on_event` function - both windows are tracked, so a plugin can
// subscribe to a new event in reaction to one it's already handling,
// not just at startup) - that's how this library knows which plugin
// owns the registration, so it can automatically remove it when that
// plugin is unloaded (frust_plugin_unload). A stale handler pointing
// into unloaded JIT code would otherwise be a use-after-free the next
// time the event fired. Registering from outside both windows (e.g. a
// raw frust_plugin_get_fn call the host makes directly) still works
// (the handler still fires) but won't be tracked for automatic cleanup
// - the caller is then responsible for not firing that event after the
// owning plugin is unloaded.
FRUST_PLUGIN_HOST_API void frust_register_event_handler(const char* name, void (*handler)(void*));

// -----------------------------------------------------------------------
// Plugin-to-plugin service discovery - docs/17-Plugin-Automation-Layer.md
// section 4.3. Everything above is two-party (host<->plugin); this is
// the third connection this library provides: one loaded plugin
// registering a capability under a name, and ANY other loaded plugin
// (or the host) looking it up by that name, without either side needing
// to already know which specific plugin provides it. Same domain-
// agnostic-by-construction stance as events - a service is a name plus
// an opaque pointer (a function pointer, a data pointer, whatever the
// registering plugin and the looking-up caller agree on out of band);
// this library never inspects it.
// -----------------------------------------------------------------------

// Registers `service` under `name`, replacing any previous registration
// for that exact name (a warning goes to stderr when that happens - two
// plugins claiming the same service name is more likely a real conflict
// than something intentional). Must be called from within a plugin's
// own frust_plugin_call_on_init() or frust_plugin_call_on_event() call,
// same as frust_register_event_handler - that's how a registration is
// attributed to the plugin that made it, so frust_plugin_unload can
// automatically remove it when that plugin is unloaded (a stale service
// pointer into unloaded JIT code would otherwise be a use-after-free
// for whoever looks it up next). Registering from outside both windows
// still works but isn't tracked for automatic cleanup.
FRUST_PLUGIN_HOST_API void frust_register_service(const char* name, void* service);

// Looks up whatever is currently registered under `name`, or NULL if
// nothing is. The caller is responsible for knowing what to do with the
// returned pointer (cast it to a function pointer and call it, treat it
// as a data pointer, ...) - this library has no way to know what any
// given service name means.
FRUST_PLUGIN_HOST_API void* frust_lookup_service(const char* name);

#ifdef __cplusplus
}
#endif
