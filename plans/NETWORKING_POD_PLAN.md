# FRust Networking Pod Plan

**Status: Deferred.** Do not begin implementation until the current FRust
release deployment is complete and verified.

## Goal

Provide a real, reusable networking layer to standalone FRust programs and
embedded FRust in Creation Suite through one shared Windows-native
implementation and one friendly FRust API. The first proof application is a
small loopback HTTP server written in FRust itself, not a C++ HTTP shortcut.

## Design Decision

Networking is not a collection of raw `extern fn` declarations inside a
source-only pod. It is the first consumer of generic native-extension support
in Frate:

1. A C++ static library, `frust_net_runtime`, owns the Windows networking
   implementation and exposes a stable C ABI.
2. A `net` FRust pod wraps that ABI in an ergonomic language-level API.
3. Frate packages, installs, and links target-specific native libraries and
   declared system libraries automatically.
4. Creation Suite links the same `frust_net_runtime` library into its embedded
   FRust host. Embedded scripts and standalone programs therefore use the same
   behavior.

## First Release Scope

The C++ `frust_net_runtime` first provides blocking TCP primitives with
explicit timeouts and useful errors:

- Initialize networking
- Connect to a host and port
- Bind a listener to a host and port
- Listen and accept a connection
- Send bytes or text
- Receive bytes or text with a caller-provided limit
- Configure connect and read timeouts
- Close a connection
- Retrieve a meaningful last-error message

Keep the native ABI small and C-compatible. Do not pass C++ types or
exceptions across the FRust boundary.

The first public FRust-facing package is `net`. It wraps the raw C ABI into
FRust-level socket, stream, listener, and response/error objects without
making HTTP decisions for the caller.

The first proof pod is `http`, implemented in FRust on top of `net`, not in
the native runtime. It provides a deliberately small loopback HTTP server:

- Bind only to `localhost`, `127.0.0.1`, or `[::1]` for the first release.
- Accept a request, parse a small HTTP/1.1 request line and headers, and send
  a status, headers, and body response.
- Support a small route table and `GET`/`POST` handling as FRust code.
- Deliberately exclude TLS, keep-alive, chunked transfer, proxies,
  authentication, streaming, and public internet exposure from this first
  proof server.

WebSocket support, richer HTTP behavior, additional protocols, and Creation
Suite product-specific APIs are later FRust packages layered above `net`.

## Protocol Roadmap

The native runtime stays intentionally small. Protocol behavior belongs in
FRust pods that can evolve independently while sharing `net`:

- `http`: first proof pod, beginning as a loopback HTTP server and later
  gaining a lightweight client.
- `smtp`, `imap`, and/or `pop3`: mail protocol pods for sending and reading
  mail once the TCP layer has proved itself.
- `websocket`: built above HTTP and `net`, not hidden in the C++ runtime.
- Future database, game, and Creation Suite service protocol pods follow the
  same model.

This is the point of the foundation: FRust implements protocols; C++ only
provides the stable Windows socket boundary and the native mechanics Frate
must link.

## Frate Native Extension Support

Extend `frate.json` with target-specific native artifacts and system linker
libraries. The exact field names should be reviewed before implementation;
the intended shape is:

```json
{
  "native": {
    "windows-x64": {
      "libraries": ["native/windows-x64/frust_net_runtime.lib"],
      "systemLibraries": ["ws2_32.lib"]
    }
  }
}
```

Required Frate behavior:

1. `frate package` includes declared native artifacts in the `.frpod`.
2. `frate install` restores them under the cached pod directory.
3. `frate build` selects artifacts for the current Windows target and adds
   them, plus declared system libraries, to its linker response file.
4. Missing or incompatible artifacts produce a direct diagnostic naming the
   pod, target, and missing file.

This is generic infrastructure, not a networking exception. It should later
support database, hardware, compression, graphics, and AI-provider pods.

## Implementation Order

1. Document and test the Frate native-extension manifest and linker flow with
   a tiny native test library.
2. Implement package/install/link support for native artifacts in Frate.
3. Add an end-to-end Frate regression test proving a packaged native pod can
   be installed, linked, and run from a clean staged release layout.
4. Build `frust_net_runtime` over Winsock with the minimal C ABI above.
5. Create the `net` FRust wrapper pod over that C ABI.
6. Create the loopback-only `http` server pod in FRust, using only `net`.
7. Link `frust_net_runtime` into Creation Suite's embedded FRust host.
8. Add a local HTTP-server integration test, documentation, and release
   packaging checks.

## Acceptance Criteria

- A clean Frate installation can install the `net` pod and build a FRust TCP
  client or listener without manual linker arguments.
- The FRust-written `http` pod can serve a known request from a local client,
  reject a non-loopback bind, and report connection, timeout, and receive
  failures clearly.
- Creation Suite embedded FRust can use the same `net` API and runtime.
- Native artifacts are packaged and resolved through generic Frate behavior,
  with no special-case networking path.
