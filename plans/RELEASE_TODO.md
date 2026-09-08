# Release Todo

## Source Distribution

**Status: Deferred until after the current `v0.5.2` publication is settled.**

Publish a source archive beside every Windows binary release.

Requirements:

- The release contains a clearly named source zip, for example
  `frust-v0.5.3-source.zip`, alongside the Windows x64 binary zip.
- It contains the full FRust source tree required to build the project,
  including checked-out submodule source rather than empty submodule links.
- It excludes local build outputs, caches, IDE files, and Git metadata.
- The release notes and Downloads Center make the binary and source downloads
  clearly distinguishable.
- The release workflow creates and uploads both artifacts automatically.

GitHub's automatically generated "Source code (zip)" is not a substitute for
this task unless it is verified to contain everything a source builder needs.
