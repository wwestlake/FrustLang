# FrustyRealScore

A small standalone Frust executable implementing a deterministic scoring engine.

## Scoring rules

A `ScoreInput` contains:

- `base_points`
- `combo_multiplier`
- `penalty`

The raw score is `base_points * combo_multiplier - penalty`. Final scores are
clamped at zero, so penalties cannot produce a negative score.

Ranks are numeric:

- rank `0`: score below `100`
- rank `1`: score from `100` through `249`
- rank `2`: score from `250` through `499`
- rank `3`: score at least `500`

The executable has deterministic self-checks for the zero floor, a mid-range
score, and a top score. `main` returns `0` only if every check passes; it
returns `1` otherwise. It intentionally does not use console output.

## Build and run

From this project directory on Windows:

```text
frate build
frate run
```

The process exit code is the verification result: `0` means all self-checks
passed, and `1` means at least one check failed.
