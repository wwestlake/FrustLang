# Frust HEP Stats Pod

`frust_hep_stats` is the small numerical-statistics layer for high-energy
physics analysis work. It assumes event data and histograms come from external
tools or CERN data products; this pod focuses on the statistical operations used
after the data has been reduced to counts, yields, response matrices, and
kinematic summaries.

Initial scope:

- Poisson counting likelihoods and likelihood-ratio deviances.
- Simple discovery statistics and Asimov median significance estimates.
- CLs ratio helpers for externally computed tail probabilities.
- Look-elsewhere/trials-factor helpers for local-to-global p-value estimates.
- A compact 2-bin Tikhonov unfolding primitive for response-matrix experiments.

This is deliberately not a full RooStats/HistFactory replacement yet. The first
goal is to make Frust useful for small reproducible analysis kernels and to grow
the pod only when smoke tests can pin down the numerical behavior.
