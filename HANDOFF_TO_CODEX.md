# Handoff to Codex — Claude session, 2026-08-24 through 2026-08-25

Date: 2026-08-25
From: Claude, repo `wwestlake/FrustLang`, branch `master`

This is a pre-emptive handoff: Claude is nearing a weekly token limit and may
go offline mid-task. Per a hard-learned lesson from a previous session (a
full day's work sat unpushed locally and had to be recovered by hand), the
standing rule now is **push everything the moment it's real, don't wait for
a natural stopping point.** Everything described below as "done" is genuinely
committed and pushed to GitHub - verify with `git log`/`gh pr list` if in
doubt, don't take this file's word for it blindly.

## Open PRs / issues (nothing urgent, but worth knowing about)

- **frust-library#1** (`fix/wire-missing-core-modules`) and **frust-library#2**
  (`feature/jit-eval-wrapper`, based on top of #1) - both open, both pushed,
  both real fixes to the `06_frust_library` submodule (core pod wiring,
  2 real pre-existing crash bugs, and a new `jit_eval_f32` wrapper). #2
  includes #1's commits, so merging #2 alone (into frust-library's `main`)
  effectively closes both.
- **FrustLang#9** (issue, not PR) - a verification checklist for the big
  PR #8 that just merged (see below). Read this before assuming anything
  in that PR's scope is fully proven.
- FrustLang PRs #1-#8 are all merged into `master` already - nothing else
  open on this repo as of this writing.

## What just happened (PR #8, merged into master)

A large, connected chain of real fixes, all triggered by trying to write an
honest GitHub wiki tutorial "First Program" lesson using real Frust (no raw
`extern fn`) instead of hello-world boilerplate. Full detail is in PR #8's
own description and its commit message - short version:

1. **New `import <pod>, "<version>";` keyword** - real cross-pod source-level
   imports (`frust.y`/`frust.l`/`AST.h`), distinct from the pre-existing
   `use somepod;` (which parses but has always been a complete no-op).
   `"current"` reads the version already declared in the importing pod's own
   `frate.json`. Resolution logic lives in `frate`'s own file-collection code
   (`collectImportedPodFiles`, `projects/05_frate/src/main.cpp`), mirroring
   how `use self::X;` resolution already worked there.
2. **Extracted `frust_runtime`** - `frust_print_str`/`format_*`/`buf_*`/
   `ast_*`/`jit_eval_f32` used to be duplicated verbatim in both
   `frust_compiler`'s and the IDE's `Main.cpp`. Now one real static library
   (`projects/01_language_paradigms/02_functional/Runtime.cpp` + its
   CMakeLists.txt target) both link.
3. **Fixed `frate`'s AOT link step**, which never actually produced a
   working standalone executable before this. Removed `/ENTRY:main` (was
   bypassing CRT startup entirely - see PR #8's commit message for the full
   "how other languages do this" reasoning), wired in `frust_runtime.lib`
   and (conditionally) `frust_plugin_host.lib` + its full LLVM/Windows
   dependency chain, switched to a linker response file (the raw argument
   list blew past `cmd.exe`'s line-length limit).
4. **`frate cache-dir` command** + configurable pod cache location
   (`projects/05_frate/include/frate/FrateCache.h`/`.cpp`) - was hardcoded
   to `%APPDATA%` (always writes to C:), now resolves env var → persisted
   `frate_settings.json` next to the exe → default next to the exe.
5. **`examples/tutorial-first-program/`** - the wiki's real "First Program"
   lesson kept in-repo, wired into `.github/workflows/release.yml` as a real
   release gate.

## What's verified vs. not (see issue #9 for the live checklist)

**UPDATE, same session, after the full rebuild finished**: all three
locally-checkable items below are now done, against the exact merged
commit, not a scratch copy:
- ✅ Full `frust_plugin_host` regression suite - all 16 examples,
  `ALL_CHECKS_PASSED` on every one.
- ✅ Fresh IDE Debug build + launch smoke test - builds clean, launches,
  stays running.
- ✅ The literal committed `examples/tutorial-first-program/` - `frate run`
  against it (not a scratch copy) produces exactly the expected output
  (`Entrance Hall` / `A cold stone hall...`), exit 0.

**Still genuinely open** - the one item that needs a real GitHub Actions
runner, can't be checked locally: a real CI run of
`.github/workflows/release.yml`'s tutorial-gate step. That only fires on a
`v*` tag push, which also publishes a real GitHub Release as a side effect
- not something to trigger casually just to test the step. Whoever pushes
the first real `v0.5.0` tag is the first real end-to-end test of it.

Original verification note (kept for the record): a scratch copy of the
tutorial example was checked first, before the full rebuild finished, and
JIT mode (Hello World, live metaprogramming) was re-confirmed working
right after the `frust_runtime` extraction, before this PR was pushed.

**If you're picking this up because Claude ran out of tokens mid-verification:**
check whether a Claude session already ran/reported on the above (look for
recent commits, or a comment/note in issue #9) before redoing it from
scratch. If nothing's there, start with the regression suite - it's the
highest-value check (catches any real regression across the whole plugin
host stack, not just the one new example).

## Real gotchas for this repo (save yourself the rediscovery time)

- **Single-core builds only.** Never pass `/m` or `/maxcpucount:N>1` to
  MSBuild on this machine - has caused real problems before. Always
  `/maxcpucount:1`.
- **MSBuild.exe lives on D:, not the usual C: path**:
  `D:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe`.
  Same for `vswhere.exe`: `C:\Program Files (x86)\Microsoft Visual
  Studio\Installer\vswhere.exe` (that one IS on C:, just MSBuild itself isn't).
- **LLVM is NOT installed via a normal vcpkg install on this machine** - it's
  at `D:\000 Creation Suite\apps\CreationEngine\vcpkg_installed\x64-windows`,
  a real, trimmed (~11GB) build, also mirrored on S3
  (`s3://creation-suite-build-cache/vcpkg-cache/llvm-vcpkg-x64-windows.tar`)
  for CI. Don't let anything trigger a fresh LLVM build from source - it's
  slow and unnecessary, the trimmed build already covers everything needed.
- **Windows only, no Linux/Unix consideration anywhere in this project,
  ever**, unless explicitly asked in the moment - a standing, deliberate,
  repeatedly-stated directive.
- **`AGENTS.md`** (repo root) has the full standing rules list - read it
  before doing anything nontrivial. Rule 10 in particular: any tutorial/doc
  example is a real use-case test of the compiler - run it through
  `frust_compiler.exe`/`frate` for real before publishing it, and if it
  reveals a real bug, stop, fix the bug, then resume - don't quietly route
  around it with a "safer" example.
- **`LANGUAGE_GAPS.md`** (`projects/01_language_paradigms/02_functional/`)
  is the authoritative, actively-maintained record of what's actually done
  vs. partial vs. open in the language itself - keep it current the moment
  something's status changes, don't let it go stale.
- **`06_frust_library` is a git submodule**, its own separate repo
  (`wwestlake/frust-library`). Commit/push order matters: submodule first,
  then the parent repo's pointer bump, always in that order, two separate
  pushes.
- **PR-based workflow, not direct push** - branch, commit, push the branch,
  `gh pr create`. The user has owner-level bypass on branch protection and
  may merge directly themselves sometimes; don't assume that's the default
  for anyone else.
- **The GitHub wiki** (github.com/wwestlake/FrustLang/wiki) has a partial
  tutorial published (Getting Started, Building from Source) that's
  mid-rewrite as a build-along "single-user dungeon crawl" project (design
  captured in a Claude plan file, not in this repo) - the
  `examples/tutorial-first-program/` pod is that rewrite's actual "First
  Program" lesson, kept in sync with whatever the wiki page says. If you
  touch the wiki, keep both in sync.

## Where things are headed (not started, just named so it isn't a surprise)

- The dungeon-crawl tutorial rewrite is far from finished - only "Getting
  Started"/"Building from Source"/"First Program" exist so far, of a
  planned ~13-lesson arc (control flow, interfaces, generics, `shared`
  ownership, save/load, an `extern`-FFI lesson calling a small planned C++
  terminal-UI library named "Agent 86", the IDE, plugins-as-mods, and an
  optional live-metaprogramming capstone).
- A `v0.5.0` GitHub Release hasn't actually been tagged/published yet - the
  CI pipeline (`.github/workflows/release.yml`) that would build and publish
  it exists and is believed correct, but has never been triggered by a real
  tag push.
- **`v0.5.0` tag pushed** (2026-08-25, same session as everything above) -
  the very first real run of the CI release pipeline, triggered live. Check
  https://github.com/wwestlake/FrustLang/actions for the result if it isn't
  already reflected in a comment on issue #9 or a later handoff note - this
  session's token budget ran out before it could see the run through to
  completion (a real 20-60+ minute Windows CI build, first time ever run).
  The workflow itself doesn't depend on this session staying alive - it's
  running entirely on GitHub's own infrastructure regardless.
- **New idea, spec being developed in a separate session (not this repo
  yet)**: a .NET host for Frust, so it could be embedded in a .NET app.
  Assessed as genuinely feasible - `frust_plugin_host`'s API is already a
  clean `extern "C"` C ABI, exactly what .NET's P/Invoke needs. The one real
  gap: it's only ever been built as a static `.lib` so far, never a `.dll` -
  P/Invoke needs a real dynamic library to load at runtime, so step one
  would be adding a DLL build target (a modest CMake addition, not a
  redesign, since the API surface is already right). Not started - whoever
  picks this up should look for the actual spec first (being written
  elsewhere) before designing anything.
