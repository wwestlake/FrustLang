# Frust Histogram Pod

`frust_histogram` provides the binned-analysis layer that sits between event
data and statistical pods such as `frust_hep_stats`.

Initial scope:

- Small fixed-size 1D histograms with uniform binning.
- Weighted fills, underflow, overflow, totals, densities, and bin centers.
- Poisson errors, residuals, pulls, and chi-square terms.
- A compact 2x2 histogram for quick eta/phi-style maps.
- Helpers that bridge binned signal/background counts into HEP counting models.

The v0.1.0 surface is intentionally fixed-size. Dynamic histogram storage can
arrive later when the language/library container story is stronger.
