# Frusty Smoke Test

Minimal standalone Frate executable used to verify that an executable pod builds successfully.

## Build

From this project directory on Windows, run:

```text
frate build
```

The entry point is `src/main.fr`, and `main` returns the numeric value `0`.

This smoke test intentionally does not use `println_*` or `console_io`. Console helpers are documented as JIT-only host exports and are not assumed to be linkable in standalone Frate builds. The build check therefore verifies compilation and linking without printed output.
