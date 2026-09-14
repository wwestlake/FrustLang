# Frust Library Authoring Notes

This is the working pattern for `frust_linalg` and the next Frust library
packs.

## Package Layout

A reusable library is a Frate lib pod:

```text
pod_name/
  frate.json
  src/
    lib.fr
    module_a.fr
    module_b.fr
```

`src/lib.fr` is the package surface and compile-order list. Every source
file that belongs to the package must be named there with `use self::...`.
A `.fr` file in `src/` is not automatically compiled.

## Dependencies

Put dependency versions in `frate.json`:

```json
"dependencies": [
  { "name": "core", "version": "1.0.1" }
]
```

Then import the pod from source:

```frust
use core;
```

Frate cross-pod imports are currently direct, not transitive. Until that
changes, an executable that imports `frust_linalg` and needs functions that
come from `core` should also declare and import `core` directly.

## Core Distribution

`core` is a standard Frust pod, not a compiler magic feature. It can be
resolved through Frate like any other pod:

- packaged as a `.frpod`
- installed into the machine cache
- fetched from the registry when published there
- bundled in a packaged Frust release under `stdlib`, where Frate can copy
  it into the writable cache on demand

Development builds are not assumed to be installed system-wide. Use the
explicit development build executable paths when invoking suite tools.

## Documentation

Each serious library pack should carry:

- a package spec under `docs/`
- a capability list grouped by module
- a test strategy with expected result values
- notes for any language/tooling limitations discovered while writing real
  library code

Do not mark a language capability as missing until the package has been
checked against `core` and other existing pods.
