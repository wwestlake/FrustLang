# FrustLang

Frust is a statically-typed, compiled language built on LLVM (JIT and
AOT), designed around zero-overhead control: real generics
(monomorphized, not type-erased), real reference counting with
automatic drop, real closures, real interface dispatch - all compiled,
never interpreted.

This repo is the whole Frust ecosystem: the compiler, the standard
library, a plugin/host system for embedding Frust in other
applications, a JUCE-based IDE, and supporting tooling (VS Code
extension, language server, a graph-to-source node compiler). It was
split out of a larger personal R&D monorepo once Frust outgrew being
one experiment among many - full commit history was preserved across
the move.

## What's actually working

Tracked honestly in
[`LANGUAGE_GAPS.md`](projects/01_language_paradigms/02_functional/LANGUAGE_GAPS.md) -
every feature below moved from "designed" to "implemented" to
"verified with a real hand-predicted test" before being called done,
and the gaps document says exactly what's still open.

- **Real generics, monomorphized**: `struct Box<T>` and generic free
  functions (`fn identity<T>(x: T) -> T`, called via explicit
  `identity::<i64>(5)` turbofish syntax) each get their own concrete
  LLVM type/function per instantiation - no boxing, no vtables unless
  you asked for one.
- **`Result<T,E>` / `Option<T>`** with real constructor sugar
  (`Result::ok::<i64,String>(x)`), built as ordinary Frust code on top
  of generics, not a compiler intrinsic.
- **Real interface dispatch** (`interface` / `impl X for Y`) via fat
  pointers, no hidden allocation.
- **`shared<T>`** with real strong reference counting and automatic
  scope-exit drop - deliberately designed to fail toward *leaking*,
  never toward a double-free, when ownership can't be proven
  statically. `own`/`raw` pointers exist alongside it for explicit,
  zero-overhead control.
- **Closures** as `{ code, env }` fat pointers, capture-by-value,
  fully compiled.
- **Live metaprogramming** - `quote`/`unquote`/`build_time` blocks
  that run and splice real AST at build time.
- Growable collections (`Vector<T>`), real pointer arithmetic/raw
  dereference, multi-file plugin support, and more.

## Repo layout

```
projects/
  01_language_paradigms/02_functional/   Frust core: grammar, AST, Codegen, the compiler
  02_juce_language_host/                 JUCE-based IDE, built on frust_plugin_host
  03_creation_dock/                      JUCE docking UI (IDE dependency)
  04_ai_provider/                        AI-provider abstraction (IDE dependency)
  05_frate/                              Frust's package manager
  06_frust_library/                      Standard library (its own submodule/repo: frust-library)
  07_vscode_frust/                       VS Code extension
  08_frust_lsp/                          Language server
  09_frust_plugin_host/                  Hot-reloadable plugin/host system - the extension
                                          mechanism the IDE (and any other app) uses to run
                                          JIT-compiled Frust plugins
  10_node_compiler/                      Graph JSON -> real Frust source; the compiler half of
                                          a planned Blueprint-style visual node layer
                                          (see NODE_LANGUAGE_DESIGN.md)
```

Each subproject with its own README has more detail - see
[`09_frust_plugin_host/README.md`](projects/09_frust_plugin_host/README.md)
and
[`10_node_compiler/README.md`](projects/10_node_compiler/README.md)
in particular.

## Building

Windows only, by deliberate standing decision - no cross-platform
consideration anywhere in this codebase.

Frust core builds via a CMake preset that supplies the LLVM/WinFlexBison/JUCE
paths for this machine's toolchain:

```bash
cd projects/01_language_paradigms/02_functional
cmake --preset windows-vcpkg
cmake --build build --config Debug
```

Other subprojects (`09_frust_plugin_host`, `10_node_compiler`,
`02_juce_language_host`) each pull in Frust core via `add_subdirectory`
using a relative path (`../01_language_paradigms/02_functional`) - the
`projects/` layout above must stay intact for those relative paths to
resolve.

`06_frust_library` is a git submodule - clone with
`git clone --recurse-submodules`, or run
`git submodule update --init` after a plain clone.

## Design philosophy

"Zero-overhead, aiming for the iron" - stated directly in
[`FRUST_LANG_SPEC.md`](projects/01_language_paradigms/02_functional/FRUST_LANG_SPEC.md)
and treated as a real constraint, not a slogan: it's the reason
generics are monomorphized rather than type-erased, and the reason
`shared<T>`'s reference counting has a hard scope limit (own's
automatic-free was deliberately left unimplemented rather than risk a
double-free in code that works today) instead of reaching for a
general-purpose GC.
