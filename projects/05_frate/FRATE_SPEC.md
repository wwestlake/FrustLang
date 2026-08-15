# Frate - Frust Package Manager Specification

Frate is Frust's package manager: it lets a project declare dependencies on
reusable code packages ("pods") by name and version, resolves them from a
local cache (falling back to a remote registry on a cache miss), compiles
and links a pod against its resolved dependencies, and gives a developer a
way to author and package their own pods.

**This document describes the implementation as built** (client library,
`frate` CLI, and the IDE's Frate dock panel all exist and build). It
supersedes the original pre-implementation draft: the client evolved past
that draft's scope while it was being built (a real build/link step and
workspace support were added, and the `pod.json`/`frate.json` split was
dropped in favor of one manifest file) - see [10](#10-history) for what
changed and why. The registry server (`lagdaemon.com/djehuti/api/frate`)
is live and matches [6](#6-registry-api-contract).

---

## 1. Goals

1. A Frust pod (a project or a library) can declare "I depend on pod X at
   version Y" in a manifest file, both by hand-editing it and via the IDE.
2. Resolving those dependencies checks a local, machine-wide cache first;
   only hits the network on a cache miss.
3. A developer can scaffold a new pod, build/run it, and package an
   existing one into a single distributable artifact.
4. The whole create -> package -> resolve loop works **with zero network
   dependency** during local development (see [7.3](#73-install-locally-no-network)).
5. `frate build`/`frate run` actually compile Frust source (via
   `frust_compiler --emit-obj`) and link the result into a real executable
   or library, using JIT infrastructure Frust already has.
6. Referencing a resolved pod's code *from inside Frust source* (`use
   somepod::something`) is **not** part of this - Frust's grammar has no
   module/import syntax yet. `frate build` links against a dependency's
   precompiled object file, not its parsed AST; making pods importable
   *code* (vs. linkable objects) is real follow-up work once that syntax
   exists.

## 2. Core Concepts

| Term | Meaning |
| :--- | :--- |
| **Pod** | The unit of reusable/runnable code: one `frate.json` manifest (name, version, type, description, exports, dependencies, optional workspace) plus `.fr`/`.fri` source files. A pod with `"type": "bin"` is directly runnable; `"type": "lib"` is meant to be depended on. There is no separate concept of a "project" - a Frust project you're actively working on is just a pod, usually `type: bin`, that happens to declare dependencies. |
| **Pod package (`.frpod`)** | A distributable artifact - an ordinary zip file containing `frate.json` plus the pod's source files. What gets packaged, installed locally, and published. |
| **Cache** | A local, machine-wide directory storing *extracted* pod packages, keyed by name+version, shared across every Frust project on the machine - not per-project, so N projects using the same pod+version only ever download/extract it once. |
| **`frate.json`** | The single manifest file, at a pod's root. Combines what an earlier draft of this spec split into `pod.json` (identity/metadata) and `frate.json` (dependencies) - see [10](#10-history). |
| **Workspace** | An optional `frate.json` field naming sibling pod directories that build together as one graph, with local members resolved directly instead of through the cache - see [5.2](#52-workspaces). |
| **Registry** | A remote HTTP service pods can be searched/fetched/published to by name/version. Live at `https://lagdaemon.com/djehuti/api/frate`. |

## 3. File Formats

All Frate config is JSON, matching every other config file already in this
codebase (`layout.json`, `repl_session.json`, `ai_config.json`).

### 3.1 `frate.json` - a pod's manifest

```json
{
  "name": "dsp_utils",
  "version": "1.0.0",
  "type": "lib",
  "description": "Common DSP helper functions",
  "exports": ["gain", "clamp", "lerp"],
  "dependencies": [
    { "name": "math_core", "version": "0.3.0" }
  ]
}
```

- `name`, `version` - required.
- `type` - `"bin"` (default) or `"lib"`. Determines the entry file
  (`src/main.fr` vs `src/lib.fr`) and, for `build`, whether the output is
  a linked executable or an object/`.lib`.
- `description` - optional.
- `exports` - list of function/type names the pod exposes. Informational
  only until Frust has import syntax (nothing enforces this list matches
  the pod's actual source yet).
- `dependencies` - `[{ "name", "version" }]`. Resolved by name+version
  exact match; **not** auto-walked transitively - see
  [5.1](#51-resolution-algorithm).
- `workspace` - optional, `{ "members": ["path/a", "path/b"] }` - see
  [5.2](#52-workspaces).

A directory with dependencies but no interesting `type`/`exports` of its
own (i.e. what you'd think of as "my project" rather than "a pod I'm
publishing") is still just a `frate.json` with `type: bin` - same file,
same schema, no separate consumer-only format.

### 3.2 `.frpod` - a packaged pod artifact

An ordinary zip file, conventionally named `<name>-<version>.frpod`:

```
frate.json
<source files, whatever layout the pod author used - e.g. src/main.fr>
```

Readable with any standard zip tool despite the custom extension (same
convention as `.jar`/`.docx`/`.apk`). Built via JUCE's `ZipFile::Builder`.

## 4. Local Cache Layout

```
<cache root>/<pod name>/<version>/
    frate.json
    <extracted source files>
    <name>.o          (present once something has built against this pod)
```

Cache root defaults to `%APPDATA%/Frate/cache` - machine-wide, not
project-local (conceptually equivalent to Cargo's `~/.cargo/registry` or
npm's global cache).

## 5. Resolution and Build

### 5.1 Resolution algorithm

Implemented by `FrateResolver`, used by both the IDE's "Resolve All" and
(as of this pass) `frate update`:

```
for each declared dependency (name, version):
    if cache contains <name>/<version>:
        mark RESOLVED (source: cache)
    else:
        GET {registryBaseUrl}/pods/{name}/{version}
        if 302 with a download URL:
            download the .frpod bytes from it
            extract into cache/<name>/<version>/
            mark RESOLVED (source: registry)
        else if 404:
            mark UNRESOLVED - "no such pod/version"
        else:
            mark UNRESOLVED - "registry unreachable" / network error
```

**Explicitly not in v1** (real, meaningful follow-ups, not oversights):
- **Transitive resolution** - a pod's own `dependencies` are recorded but
  not walked/auto-resolved. Every dependency a pod needs must be declared
  directly in its own `frate.json`.
- **Version ranges** - exact version strings only, matched exactly.
- **Auto-compiling a resolved dependency's source.** `frate build` expects
  a dependency's object file (`<cache>/<name>/<version>/<name>.o`, or a
  workspace member's `build/<name>.o`) to already exist. Resolving a pod
  from the registry does not compile it - today that object file only
  exists if the dependency was built locally (e.g. as a workspace member)
  before being packaged/installed. Compiling a bare-source dependency
  on-demand at build time is a real gap, not yet implemented.

### 5.2 Workspaces

`frate.json` may declare:

```json
{ "workspace": { "members": ["cli", "lib_core"] } }
```

`frate build`/`frate run`, run from the workspace root, build every member
in declared order, resolving inter-member dependencies from each other's
`build/<name>.o` directly instead of the cache. This is how a multi-pod
project (e.g. a CLI pod depending on a local lib pod) builds without a
publish/install round-trip during development.

### 5.3 Build and run

`frate build` (any pod, or every workspace member):
1. Resolves each declared dependency's object file - from a workspace
   sibling's `build/` dir if it's a workspace member, otherwise from
   `<cache>/<name>/<version>/<name>.o` (erroring if not cached - `frate
   update` first if needed).
2. Compiles the pod's own entry file (`src/main.fr` or `src/lib.fr`) via
   `frust_compiler --emit-obj` into `build/<name>.o`.
3. For `type: bin`, links every collected object file into
   `build/<name>.exe` - via `clang` if it's on PATH, else by locating
   MSVC's `link.exe` through `vswhere.exe` + `VsDevCmd.bat`. For `type:
   lib`, stops after producing the object file.

`frate run` does the above then executes the resulting binary (workspace:
only the last member in `members` order is run - it's assumed to be the
entry point, everything before it should be `type: lib`).

## 6. Registry API Contract

Live on `lagdaemon.com` (Djehuti.Api, `Program.fs`), metadata in Postgres
(`frate_pods` / `frate_pod_versions`, migration 82), package bytes in S3.

**Resolve (public, unauthenticated):**

```
GET {baseUrl}/pods?q={substring}
```
- `200 OK` - JSON array of `{ name, description, latestVersion, license,
  exports }`, one entry per pod (its most recently published version). `q`
  is an optional case-insensitive substring match on pod name; omitted or
  empty returns every pod.

```
GET {baseUrl}/pods/{name}/{version}
```
- `302 Found` - `Location` header is a short-lived (15 min) presigned S3 GET
  URL; the client fetches the actual `.frpod` bytes from there.
- `404 Not Found` - no such name/version.

`baseUrl` defaults to `https://lagdaemon.com/djehuti/api/frate` (verified
live) in both `FrateRegistryClient`'s constructor and the IDE panel;
configurable per-instance for pointing at a different/local registry.

**Publish (authenticated, requires the `frate`/`publisher` role):**

```
POST {baseUrl}/pods/{name}/{version}/upload-url
```
- Header: `Authorization: Bearer <token>`. Response: `{ presignedUrl,
  s3Key }` - a presigned S3 PUT URL (15 min) the client uploads the raw
  `.frpod` bytes to directly.
- `403 Forbidden` if `{name}` is already owned by a different publisher.

```
POST {baseUrl}/pods/{name}/{version}
```
- Body: `{ description, exports, dependencies, license, s3Key, sizeBytes }`
  (the `s3Key` returned by `upload-url` above, after the client has PUT the
  bytes there).
- `200 OK` with the recorded version's metadata on success.
- `400 Bad Request` - license not on the accepted allow-list (still a
  placeholder set in `FratePodRepository.allowedLicenses` - not yet
  reviewed with the user).
- `403 Forbidden` - pod name owned by a different publisher.
- `409 Conflict` - that exact name+version already published (versions are
  immutable, no update/overwrite path).

## 7. Authoring Workflow (producer side)

### 7.1 Create
```
frate new <pod_name> [--lib]
```
Scaffolds `<pod_name>/frate.json` (name, version `1.0.0`, the given type,
placeholder description) plus `<pod_name>/src/main.fr` or `src/lib.fr`
(bin gets a `fn main() -> i64 = { 0 }` stub).

### 7.2 Package
```
frate package
```
Run from a pod's root. Zips `frate.json` + every `.fr`/`.fri` file under
the directory into `<name>-<version>.frpod`, next to the pod directory.

### 7.3 Install locally (no network)
```
frate install
```
Run from a pod's root, after `frate package` has produced the `.frpod`
next to it. Extracts it straight into `<cache>/<name>/<version>/`. This is
what closes the *entire* loop without ever touching the registry:

```
create -> package -> install locally -> declare as a dependency in
another pod's frate.json -> resolve (cache hit) -> done
```

### 7.4 Update (resolve dependencies from the registry)
```
frate update
```
Run from a pod's root. For each declared dependency not already cached,
hits the live registry (`FrateResolver`, same code path the IDE's
"Resolve All" uses) and installs it to the cache on success.

### 7.5 Publish
```
frate publish [license]
```
Run from a pod's root, after `frate package`. Requests an upload URL,
PUTs the `.frpod` bytes to S3, then calls the publish endpoint (license
defaults to `MIT` if omitted - see the open allow-list question in
[6](#6-registry-api-contract)). Requires being signed in with the
`frate`/`publisher` role.

**Auth**: there is one desktop sign-in surface, the IDE's OAuth login
(`Auth/DesktopAuthSession`, Account menu), which persists a token to
`%APPDATA%/LagDaemonResearchIDE/desktop-auth.json`. `frate publish` reads
that same file rather than implementing a second login flow - sign in via
the IDE once, and both the IDE's Producer panel and the CLI can publish.
If that file has no valid (unexpired) token, `frate publish` fails with a
message to sign in through the IDE first. A CLI-native login (`frate
login`, writing to something like `~/.frate/credentials`) is a reasonable
future add but isn't built.

## 8. IDE Integration

`FratePanel` (`projects/02_juce_language_host/Source/FratePanel.{h,cpp}`),
docked in the IDE, two tabs:

- **Consumer** (`ConsumerView`) - reads the open project's `frate.json`,
  lists each declared dependency with live status (cached / fetched /
  unresolved, from `FrateResolver`), an "Add Dependency" form that appends
  to `frate.json`, and a "Resolve All" button.
- **Producer** (`ProducerView`) - a form (name, version, description)
  driving [7.1](#71-create); "Package...", "Install to Local Cache", and
  "Publish to Registry" buttons for [7.2](#72-package)-[7.5](#75-publish).
  Publish is gated on `DesktopAuthSession::hasValidSession()` and reuses
  its token directly (no file round-trip needed, unlike the CLI).

## 9. Explicitly Out of Scope For Now

- **`use pod::thing` import syntax in Frust.** Pods become linkable object
  files, not yet importable *source* - needs a real grammar/codegen
  extension first (see [1.6](#1-goals)).
- **Transitive dependency resolution**, **version ranges**, and
  **auto-compiling a resolved dependency's source before linking** - see
  [5.1](#51-resolution-algorithm).
- **A settled license allow-list.** Still a placeholder, not yet reviewed
  with the user.
- **Self-service publisher role grants.** Requires an admin to run
  `Permissions.grantContextRole` directly - no request/approval UI.
- **A CLI-native login flow.** `frate publish` piggybacks on the IDE's
  session file rather than having its own - see [7.5](#75-publish).

## 10. History

This spec originally (pre-implementation) called for a `pod.json`
(pod-own metadata) / `frate.json` (consumer dependency list) split, and
scoped Frate as "make pods resolvable data on disk" with no build/link
step and no workspace concept. While being implemented, the client
consolidated to one `frate.json` manifest (simpler, and it's what the code
actually shipped with) and grew a real `frust_compiler`-backed
build/link/run path plus Cargo-style workspaces, none of which were in the
original draft. That drift went unnoticed for a couple of sessions because
none of it was committed to git along the way. This revision documents the
client as built rather than reverting it - the build/workspace direction
was judged more useful than the original narrower scope - and fixes the
two places implementation had lagged the client's own design: `frate
update` was hitting a hardcoded `localhost:8080` mock instead of the live
registry, and `frate publish` was an unimplemented stub, despite
`FrateRegistryClient` and the IDE's Producer panel already doing the real
thing.
