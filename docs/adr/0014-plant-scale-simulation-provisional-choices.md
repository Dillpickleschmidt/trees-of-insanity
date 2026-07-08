---
status: accepted
---

# Plant-scale simulation fills paper gaps with provisional choices

`toi::plant::develop_plant` implements SS 5.2. Several quantities the paper leaves
unspecified are given provisional values so the model is bounded, deterministic, and
develops every built-in plant type across its lifespan. Each is a `NOTE(decision)` in
`src/toi/plant/src/plant.cpp` and is revisited once rendered previews allow visual tuning.

- **Shedding threshold `v̄_min`** = `0.02 · root_max_vigor`. Not in Table 4; a fraction of
  the vigor budget bounds module count (~1/fraction) uniformly across presets.
- **`f_collisions` normalization** — sphere overlap divided by the module's own bounding
  sphere volume (SS eq1 gives no units; prototypes are import-scaled). Direct parent/child
  adjacency is excluded — structural attachment is not crowding.
- **Main child (BH eq2)** = child at the deepest terminal node (largest node physiological
  age), a topological proxy for the axis continuation; the paper leaves module-scale main
  designation implicit.
- **Intra-module terminal split** — module vigor is split equally across terminal nodes
  (uniform light `q(n_i)=Q(u)/#n`), approximating the intra-module Borchert-Honda pass.
- **Age time-scale** — module age is integrated as `da_u = age_scale · Υ(u) · dt`, where
  `age_scale` makes a fully-vigorous module reach ~8 root-maturation ages over the lifespan.
  This reconciles unscaled growth rates (`g_p`) with import-scaled geometry so every species
  develops across its slider range. Absolute plant-scale time units are unspecified.
- **Integration** — fixed `dt = 1` with a final partial step so the clock lands exactly on the
  requested `plant_age` (capped at 1200 steps); `develop_plant` re-develops from age 0 each call. A
  final vigor pass makes returned module vigor consistent with the shedding rule and resolves
  senescence at the returned age. Pure and deterministic; a stepping interface is deferred if scrub
  performance requires it.

## Consequences

- Plant size and shape depend on these constants; they are tuning knobs, not paper values.
- The per-species time-scale and shed fraction chiefly control how growth spreads across the
  development slider and the maximum module count.
- Revisit after Piece 2 renders plants and the shapes can be judged visually.
