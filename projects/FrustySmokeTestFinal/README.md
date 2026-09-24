# Frusty Smoke Test

Minimal standalone Frate executable used to verify that a numeric Frust entry point can build successfully.

## Build

From this project directory, run:

```text
frate build
```

The entry point is `src/main.fr`, and `main` returns the integer `0`.

## Console output

This smoke test intentionally does not use `println_*` or `console_io`. Standalone Frate builds currently do not assume that the core console helpers are linkable; those helpers are documented as JIT-only host exports. The numeric return value is therefore used as the deterministic build check.
