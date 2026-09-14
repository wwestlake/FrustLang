# Frust Physics Pack R&D Spec

## Purpose

`frust_physics` is a general computational physics library for Frust. It is
not a game-physics package.

The pack should give Frust programmers reusable building blocks for
simulation: integrators, equation solvers, particles, fields, constraints,
Hamiltonian systems, mass-spring systems, diffusion/wave-style stepping,
and diagnostics for numerical quality.

The guiding idea is: model equations clearly, step them with known
integrators, measure error/energy/drift, and keep the library small enough
to verify.

Dependencies:

- `core` for standard math
- `frust_linalg` for vectors, matrices, fixed-size arrays, and math helpers

## Scope

This library is about simulation numerics and physical systems:

- ordinary differential equations
- second-order motion equations
- N-body and particle systems
- oscillators
- damped systems
- mass-spring systems
- constraints
- scalar/vector fields
- simple PDE stepping patterns
- statistical physics
- stochastic processes
- Monte Carlo simulation
- linear and nonlinear equation solving
- diagnostics and validation

This library is not centered on:

- gameplay collision stacks
- character controllers
- vehicle helpers
- game event callbacks
- broad-phase engine architecture
- contact manifolds as the primary product

Collision/contact math may appear later as one physical phenomenon among
many, but it is not the center of this pack.

## Package Shape

The package is a Frate lib pod:

```frust
use core;
use frust_linalg;
use frust_physics;
```

While Frate imports are direct and non-transitive, consumers should declare
all three dependencies:

```json
"dependencies": [
  { "name": "core", "version": "1.0.1" },
  { "name": "frust_linalg", "version": "0.1.0" },
  { "name": "frust_physics", "version": "0.1.0" }
]
```

Suggested module layout:

```text
frust_physics/
  frate.json
  src/
    lib.fr
    constants.fr
    state.fr
    integrators.fr
    ode.fr
    solvers.fr
    constraints.fr
    particles.fr
    springs.fr
    fields.fr
    stochastic.fr
    statphys.fr
    monte_carlo.fr
    particle_stats.fr
    diagnostics.fr
```

## V0.1 Capability Set

### Constants and Units

Types:

- `PhysicsConstants`
- `UnitScale`

Capabilities:

- common constants: gravitational constant, standard gravity, tau/pi helper
  aliases if useful
- timestep helpers
- scalar tolerances
- simple unit-scale conversion helpers for meters/seconds/kilograms-style
  systems

V0.1 does not need a full dimensional-analysis type system. It should at
least document expected units and make timestep/tolerance choices explicit.

### State Types

Types:

- `State1f`
- `State2f`
- `State3f`
- `Phase1f`
- `Phase3f`

Capabilities:

- scalar state
- vector state
- phase-space state: position plus velocity
- add/subtract/scale helpers
- weighted sum helpers for integrator stages

These are the basic containers for ODE stepping without needing a generic
vector-space abstraction on day one.

### First-Order ODE Integrators

Capabilities:

- explicit Euler
- midpoint / RK2
- RK4
- semi-implicit Euler where the state is position/velocity

APIs should be plain and concrete first:

- scalar derivative step
- `Vec3f` derivative step
- phase-space second-order step

RK4 matters because it is the practical baseline for many educational and
research simulations. Euler still matters because it is useful as a
reference and for tests.

### Symplectic and Verlet Integrators

Capabilities:

- symplectic Euler
- velocity Verlet
- position Verlet
- leapfrog

These are essential for physical systems where energy behavior matters:
orbits, oscillators, springs, molecular-style particles, and Hamiltonian
systems.

The pack should include energy-drift tests, not just position-value tests.

### Adaptive Integrators

V0.1 should design for but may defer full adaptive stepping.

Future capabilities:

- RK4 half-step error estimate
- RK45 / Dormand-Prince
- min/max timestep clamp
- absolute and relative tolerance
- rejected step reporting

Adaptive stepping is important, but fixed-step integrators are easier to
verify first in Frust.

### Implicit Integrators

V0.1 should include the API shape and simple scalar examples.

Capabilities:

- backward Euler for scalar equations
- implicit midpoint or trapezoid / Crank-Nicolson later
- Newton iteration hook for implicit solves

Implicit methods are important for stiff systems, damping, diffusion, and
spring networks. They require nonlinear/linear solvers, so the first pass
should keep them small and tested.

### Linear Solvers

Types:

- `LinearSolveResult`

Capabilities:

- 2x2 direct solve
- 3x3 direct solve
- Jacobi iteration for small systems
- Gauss-Seidel iteration
- conjugate gradient for symmetric positive definite systems later

These solvers support constraints, implicit stepping, diffusion, and
minimization-style physics.

V0.1 can start with scalar/2x2/3x3 and iterative examples before trying to
generalize matrix sizes.

### Nonlinear Solvers

Types:

- `RootSolveResult`

Capabilities:

- bisection
- secant
- Newton-Raphson
- fixed-point iteration

Use cases:

- implicit integrator equations
- equilibrium solves
- constraints
- inverse problems

The API should expose iteration count, convergence flag, residual, and last
value.

### Constraints

Types:

- `DistanceConstraint3f`
- `ConstraintSolveResult`

Capabilities:

- projection of a point onto a constraint
- distance constraint between two particles
- simple Lagrange multiplier solve for one scalar constraint
- SHAKE-style position projection
- RATTLE-style velocity correction later

This is physics/numerics constraint solving, not game contact solving.

Primary use cases:

- pendulums
- linked particles
- rods
- molecular-style fixed bond lengths
- constrained mechanisms

### Particles and N-Body Systems

Types:

- `Particle3f`
- `NBodyPair3f`

Capabilities:

- position
- velocity
- acceleration/force
- mass and inverse mass
- force accumulation
- gravitational pair force
- Coulomb-like inverse-square pair force
- drag
- integrate a particle with selected integrator
- total kinetic energy
- potential energy helpers for simple pair fields
- center of mass
- total momentum

N-body work is a natural Frust showcase because it uses arrays, vectors,
integrators, and diagnostics without needing engine machinery.

### Oscillators and Springs

Types:

- `Oscillator1f`
- `Spring3f`

Capabilities:

- simple harmonic oscillator
- damped harmonic oscillator
- driven oscillator later
- Hooke spring force
- damped spring force
- spring potential energy
- period/frequency helpers

These are excellent validation systems because many have known analytic
solutions or conserved quantities.

### Fields

Types:

- `ScalarFieldSample3f`
- `VectorFieldSample3f`

Capabilities:

- constant vector field
- radial inverse-square field
- uniform gravity field
- simple potential-to-force helper for known formulas
- sample particle acceleration from a field

Future:

- grid fields
- interpolation
- gradients/divergence/curl
- finite difference stencils

### Simple PDE-Style Steppers

V0.1 can include design and perhaps a small scalar fixed-array example.

Capabilities:

- 1D diffusion explicit step
- 1D wave equation explicit step
- finite difference laplacian for fixed arrays

These should use `Array<f32, N>` once the sizes are known. This is a good
second-wave feature because it tests fixed arrays in real numerical code.

### Statistical Physics

Types:

- `EnsembleSample`
- `ThermoSample`
- `Histogram1f`
- `RunningStats1f`

Capabilities:

- mean
- variance
- standard deviation
- covariance for paired samples
- running statistics
- histogram binning
- Boltzmann factor
- partition-function helpers for small discrete systems
- expectation value over weighted states
- entropy for discrete probability distributions
- temperature / beta conversion helpers
- heat capacity estimate from energy variance

Statistical physics should be a first-class part of the pack because many
physical systems are too large or too noisy to model as a single clean
trajectory.

### Stochastic Processes

Types:

- `RandomWalk1f`
- `RandomWalk3f`
- `BrownianState1f`
- `LangevinState1f`

Capabilities:

- 1D and 3D random walk step
- Brownian motion step
- Langevin dynamics step
- Ornstein-Uhlenbeck process
- white-noise sample scaling by timestep
- diffusion coefficient helpers
- mean-squared displacement
- autocorrelation helper later

This layer should build on `core` random support where possible. If the
existing random API is not enough, add small deterministic PRNG helpers as
library code rather than treating it as a language blocker.

### Monte Carlo Methods

Types:

- `MonteCarloResult`
- `MetropolisState`

Capabilities:

- direct Monte Carlo averaging
- rejection sampling helper
- Metropolis accept/reject probability
- Metropolis step over a scalar state
- simple simulated annealing schedule
- estimate integral over an interval
- estimate expectation over a discrete weighted ensemble

Future:

- Markov-chain diagnostics
- burn-in/thinning helpers
- autocorrelation time estimate
- bootstrap resampling
- replica exchange

Monte Carlo support is important for statistical mechanics, integration,
uncertainty estimation, and optimization-style physical models.

### Particle Physics Statistics

Types:

- `ParticleSelectionScore`
- `DistributionCut1f`

Capabilities:

- Gaussian PDF
- z-score / pull
- chi-square contribution
- symmetric mass-window cuts
- invariant-mass window scoring later
- signal/background ratio
- Asimov-style counting significance
- likelihood ratio helpers
- simple cut efficiency and purity helpers
- confusion-count helpers: true positive, false positive, false negative

This is for particle-data analysis: isolating candidate particles or events
from measured distributions. It belongs with statistical physics and Monte
Carlo, not with game-style collision logic.

### Diagnostics

Types:

- `EnergySample`
- `SimulationDiagnostics`

Capabilities:

- kinetic energy
- potential energy
- total energy
- linear momentum
- center of mass
- residual/error norms
- max absolute error
- RMS error
- drift over time
- sample mean/variance
- histogram sanity checks
- ensemble expectation checks
- autocorrelation estimates later

Diagnostics are first-class. A physics pack without error, energy, and
momentum checks is just a pile of steppers.

## Future Capability Map

- adaptive RK45 / Dormand-Prince
- implicit midpoint and BDF-style methods
- conjugate gradient over fixed arrays
- sparse matrix representation
- finite difference grid fields
- finite volume helpers
- finite element research primitives
- symplectic splitting methods
- constraint systems with multiple coupled constraints
- SHAKE/RATTLE for many-particle systems
- variational integrators
- stochastic integrators
- Langevin and Brownian dynamics
- Monte Carlo and Metropolis-Hastings helpers
- statistical ensemble tooling
- bootstrap/jackknife error estimates
- Kalman/filtering helpers
- optimization/minimization helpers
- nondimensionalization helpers
- deterministic replay/test harness

## Test Strategy

Every test pod should return a hand-predicted integer score, following the
current Frust/Frate smoke-test convention:

- integrator smoke test:
  validates Euler, RK2, RK4, symplectic Euler, Verlet/leapfrog on simple
  systems with known values
- oscillator smoke test:
  validates harmonic oscillator stepping and approximate energy behavior
- solver smoke test:
  validates bisection/secant/Newton on known roots and direct 2x2/3x3
  linear solves
- constraint smoke test:
  validates distance projection and simple constrained particle behavior
- n-body smoke test:
  validates pair forces, center of mass, momentum, and energy helpers
- field smoke test:
  validates uniform and radial field sampling
- PDE smoke test:
  validates one diffusion/wave step on a tiny fixed array
- statistical physics smoke test:
  validates running mean/variance, Boltzmann weights, expectation values,
  and entropy on tiny hand-computed distributions
- stochastic smoke test:
  validates deterministic seeded random-walk/Brownian/Langevin paths where
  the generated samples are known
- Monte Carlo smoke test:
  validates accept/reject logic, simple integration estimates with fixed
  samples, and weighted discrete expectations

Floating-point checks should use exact values when possible and
`approx_eq_f32` from `frust_linalg` for iterative cases.

## Build Order

Recommended implementation order:

1. Scaffold `frust_physics` as a Frate lib pod depending on `core` and
   `frust_linalg`.
2. Add scalar/vector state helpers.
3. Add explicit Euler, RK2, RK4 for scalar and `Vec3f` states.
4. Add phase-space symplectic Euler and velocity Verlet.
5. Add oscillator and spring helpers.
6. Add diagnostics: energy, momentum, error norms.
7. Add root solvers: bisection, secant, Newton.
8. Add tiny linear solvers: 2x2/3x3 direct solve.
9. Add particle/N-body helpers.
10. Add distance constraints and projection.
11. Add first tiny PDE/fixed-array stepping example.
12. Add running statistics, histograms, and small ensemble helpers.
13. Add deterministic stochastic-process helpers.
14. Add first Monte Carlo helpers.
15. Package/install and run smoke tests from the direct development
    `frate.exe` path.

## Known Frust/Frate Considerations

- Cross-pod imports are direct, not transitive. Tests and consumers should
  declare/import `core`, `frust_linalg`, and `frust_physics`.
- The current Frate smoke-test convention uses score exit codes, so nonzero
  expected scores look like process failures. Record expected scores
  explicitly in docs.
- Prefer concrete structs and free functions until Frust's module, generic,
  and collection ergonomics are stronger.
- Use `Array<T, N>` deliberately for fixed numerical kernels where it makes
  sense, especially PDE stencils and small fixed systems.

## References

- Hairer, Lubich, and Wanner: geometric numerical integration and long-term
  behavior of Hamiltonian systems.
- Butcher: Runge-Kutta methods and numerical ODE solving.
- Press et al.: practical root-finding, linear solving, and error analysis
  patterns.
- Molecular dynamics literature: velocity Verlet, leapfrog, constraints,
  SHAKE/RATTLE, and energy drift diagnostics.
- Statistical mechanics literature: ensembles, partition functions,
  Boltzmann weights, entropy, fluctuations, and response estimates.
- Monte Carlo literature: direct sampling, Metropolis methods, Markov
  chains, sampling error, and ensemble averages.
- Computational physics teaching literature: oscillators, N-body systems,
  diffusion, wave equations, random walks, Monte Carlo integration, and
  finite difference stepping.
