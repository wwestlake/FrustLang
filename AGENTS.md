# AGENTS.md

Rules for any AI agent (Claude, Codex, Gemini, etc.) working in this repo.

This repo was migrated out of a larger personal R&D monorepo
(`lagdaemon-tech-research`) on 2026-08-24 once Frust outgrew being one
experiment among many - full commit history preserved via
`git filter-repo`. Everything below carries forward from that repo's
own hard-won `AGENTS.md`, adjusted for this repo now being Frust-only.

## Platform target - read this first

**Windows only. No Linux/Unix/cross-platform consideration, ever, for
any part of this project — the language, the compiler, the IDE, the
standard library — unless the user explicitly asks for it in that
specific moment.** Do not pause, defer, or scope a feature down
because "it wouldn't be portable" or "there's no platform-conditional
compilation mechanism yet." Do not add `#ifdef`/`#cfg`-style
portability branches, do not write code with an eye toward a future
Linux port, do not note "Windows-only for now" as a caveat implying
it should change later. Build it for Windows and ship it.

The user has stated this directly, repeatedly, across sessions in the
old repo ("I have said this umpteen times... it does not need to be
compatible with unix, stop that, its not a consideration for you now"
- 2026-08-22). Treat it as settled, not open for reconsideration. If
you notice yourself about to hedge on cross-platform grounds, that is
the signal to stop and just build the Windows version.

## Build

- **Debug builds, not Release.** Build/run with `--config Debug`, not `Release`, unless explicitly told otherwise for a specific test.
- **Single-core builds only.** Never pass `/m` or `/maxcpucount` to MSBuild (or equivalent parallel-build flags to other build tools) on this machine — this is the user's own machine and they need it usable while a build runs. Plain `MSBuild.exe solution.sln /t:target /p:Configuration=Debug`, no parallelism flag, every time.
- Frust core configures via a CMake preset (`windows-vcpkg`) that supplies the LLVM/WinFlexBison/JUCE paths for this machine - `cmake --preset windows-vcpkg` from `projects/01_language_paradigms/02_functional`, not a bare `cmake ..` (which will fail to find LLVM - the preset is what actually sets `CMAKE_PREFIX_PATH`).
- Other subprojects (`09_frust_plugin_host`, `10_node_compiler`, `02_juce_language_host`) pull in Frust core via `add_subdirectory` using a relative path up to `01_language_paradigms/02_functional` - the `projects/` layout must stay intact for these to resolve.

## Standard workflow (every change, no exceptions)

1. Make the change.
2. Build it (Debug config) and actually run/verify it — don't claim done on compile-success alone. For a language/codegen change, write a real test with a hand-predicted expected result BEFORE running it, then run and confirm the match.
3. Run the full regression sweep (`09_frust_plugin_host`'s example suite, `02_juce_language_host` rebuild) when the change touches core codegen/grammar paths - not just the one new test.
4. `git status` / `git diff` — check what's actually changed before staging.
5. Commit, with a real message explaining why, not just what.
6. Push to `origin` — the user has standing authorization to push routinely ("push early, push often"); still use judgment on force-push/branch-deletion style destructive ops, those always need explicit sign-off.
7. If it's a submodule (`projects/06_frust_library`, its own repo at `wwestlake/frust-library`), commit+push inside the submodule first, THEN commit+push the parent repo's pointer bump. Two separate pushes, always in that order.
8. If you told the user a rule/process applies going forward, write it into this file, not just into your own private memory. This file is what's authoritative and visible in the repo — private memory is a supplement, never a substitute.
9. Keep `LANGUAGE_GAPS.md` (`projects/01_language_paradigms/02_functional/`) current the moment an item's status changes - this document existing isn't enough on its own, the standing failure mode it was built to fix is real progress happening underneath it while the doc goes stale.
10. Wiki/tutorial/doc examples are a real use-case test of the compiler, not just prose: run every example through `frust_compiler.exe` (hand-predicted output vs. actual) before it goes in a page. If an example reveals a real bug - not just an already-documented gap - stop the doc work immediately, log it in `LANGUAGE_GAPS.md`'s "CRITICAL BUGS" section if it's silent/incorrect-output-class (jumps the queue ahead of normal numbered gaps), fix it, verify the fix, then resume using the corrected pattern. Don't quietly route around a found bug with a "safer" example instead.

## Git discipline

- **Commit AND push proactively, both, every time.** After every verified build/change: commit right then, push to `origin`/`main` right after. Don't wait to be asked for either one.
- **This repo has no deployment - it's just a git repo.** `main` is the working branch; pushing straight to it carries no CI/CD or production risk. Treat pushing to `main` as routine, not something to hesitate over.
- Never force-push, never `--no-verify`, never skip hooks without being told to.
- Never summarize a set of tracked work (a gap list, a checklist) as "done"/"closed"/"complete" if even one item in it is genuinely PARTIAL - the summary sentence must reflect the worst status in the set, not the majority. Say "N of M done, rest open" plainly.

## Communication

- **Don't silently downgrade a possibly-transient failure into "it's gone" before an expensive fallback.** Say the assumption out loud first, especially before long rebuilds.
- **State the target machine/file directly instead of asking the user to disambiguate** when context already makes it obvious.
- **Don't ask the user to make implementation-level technical decisions** — make the call and proceed. Only surface real goals/priorities as questions.
- **Discuss real language/tooling design choices before implementing them** — don't just build and present as done. Reserve this for genuine architectural calls (a new type-system feature, a new codegen representation), not mechanical fixes.
- **Critical handoff info (passwords, IPs, key facts) gets its own short, clearly labeled block** — never buried in a paragraph.
- **Never trigger UAC/sudo/admin elevation beyond already-agreed scope without discussing first**, every time, no exceptions.

## Toolchain

- LLVM 18.1.6 via vcpkg, WinFlexBison, and JUCE are all machine-level installs referenced by absolute path in `CMakePresets.json` - not part of this repo, don't try to vendor them in.
