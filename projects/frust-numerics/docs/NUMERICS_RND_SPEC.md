# Frust Numerics R&D Spec

## Purpose

`frust_numerics` is the general numerical methods pod for Frust. It holds reusable scalar numerics that should not live inside domain-specific pods such as `frust_physics`.

The first version is intentionally sample-based and fixed-width. Frust does not yet have the higher-order function, closure, or general collection support needed for ergonomic arbitrary-function integration or vectorized numeric APIs. This pod uses small, explicit function signatures that compile today and can later become the verified foundation for richer APIs.

## v0.1.0 Scope

- interpolation and remapping
- smoothstep and smootherstep over arbitrary ranges
- cubic Hermite interpolation
- trapezoid and Simpson sample-area helpers
- finite-difference derivative estimates
- simple scalar integration step helpers
- Newton and bisection step primitives
- fixed-width mean, variance, covariance, and correlation
- fixed-width signal RMS, peak, and normalization helpers

## Dependencies

- `core` `1.0.1`

## Verification

The workspace contains three executable smoke pods:

- `tests/interpolation_smoke`
- `tests/calculus_smoke`
- `tests/stats_signal_smoke`

Each smoke test returns the number of passing assertions as its process exit code. A passing run must match the hand-counted number of `pass_if(...)` calls in the test body.

## Future Work

- Move general solver/statistical helpers out of `frust_physics` once downstream compatibility is clear.
- Add linalg-aware numerical helpers after `frust_linalg` and `frust_numerics` are both registry-published.
- Add array/buffer APIs when Frust has the right library-level collection abstractions.
- Add function-pointer or closure-based generic root finding and integration after the language support is settled.
