# Growth library context

This package implements renderer-independent branch-module development, paper equations, and deterministic single-plant simulation derived from Synthetic Silviculture.

## Language

- **Branch module prototype**: reusable branch-node/segment topology and developmental attributes.
- **Plant age**: elapsed simulation time in years.
- **Module physiological age**: developmental state `a_u`, not calendar time; it advances from vigor-dependent growth rate through Eq. 6.
- **Growth snapshot**: visible state of one module at a physiological age.
- **Plant snapshot**: immutable lightweight view of current plant modules, geometry, and diagnostics, valid until the next plant step or reset.
- **Mature module**: module whose physiological age has reached its fully grown age; eligible terminal attachments are committed at the end of that crossing plant step.
- **Module ID**: monotonically increasing plant-local integer assigned at attachment, stable for the module's lifetime and never reused after shedding. It is machine identity, not label content.
- **Plant type**: complete Table 4 parameter set; never call it a species in library code.
- **Vigor**: one module's growth potential `v̄(u)`.
- **Maximum module vigor**: shared per-module scale `v̄_max = 1`; it is distinct from a plant type's maximum root vigor. It normalizes the Eq. 5 growth rate and morphospace `D'` only, so a module's vigor may exceed it.
- **Module-scale terminal vigor**: per-terminal vigor `v` inside a mature module, distributed over that module's own direct exposure divided equally across its terminals. It gates attachment and is distinct from plant-scale `v̄(u)`.
- **Minimum module vigor**: provisional shared cutoff `v̄_min = 0.02` used by growth, attachment, and shedding until whole-plant calibration is possible.
- **Shedding**: removal of a below-minimum-vigor module and its attached descendant subtree before surviving modules grow or attach children in that plant step.
- **Root vigor**: the root module's vigor, equal to its accumulated light capped by the plant type's maximum root vigor. It is the whole plant's vigor flux, divided without loss among descendants.
- **Maximum root vigor**: plant type's Table 4 cap `v̄_rootmax` on the root vigor budget.
- **Direct light exposure**: one module's available light/space estimate `Q(u)`, derived from raw bounding-sphere intersections with every other module except itself.
- **Accumulated light**: a module's direct light exposure plus the accumulated light of all its attached child subtrees. Mature modules route this through terminal and branch topology; an immature leaf module's accumulated light equals its direct light exposure.
- **Module bounding sphere**: sphere enclosing a module's currently developed points, centered at their module-local axis-aligned bounds midpoint with radius equal to the furthest point. The rigid module transform places that sphere in plant space, so its size is orientation-invariant and depends on spatial extent rather than branching endpoint density.
- **Basipetal pass**: tip-to-root accumulated-light calculation over the module tree.
- **Acropetal pass**: root-to-tip vigor distribution over the module tree, dividing each module's vigor among its child modules at module intersections.
- **Morphospace**: fixed apical-control/determinacy grid used by the pure selection policy.
- **Main axis**: precomputed dominant continuation through a prototype fork. Children within 10 degrees of the straightest continuation are treated as equivalently aligned, then resolved by pipe-diameter factor, exact alignment, longest downstream path, and stable segment order. Following these continuations identifies one main-axis terminal; all other terminals are lateral.
- **Lateral group**: all non-main children at one fork, treated as one side of the Borchert-Honda split before their shared vigor is divided proportionally by accumulated light.
- **Module orientation**: rigid transform selected once when a new module attaches, starting with its root segment aligned to the mature parent terminal tangent and evaluating the new module's precomputed mature prototype extent. Same-step siblings are oriented main-axis first, then by decreasing mature-tangent alignment with the main tangent; later siblings account for earlier selected mature bounds. Collision weight is the fixed baseline `ω₁ = 1`; plant-type `ω₂` controls tropism relative to it. Search uses additive Z-up XYZ Euler angles, at most three iterations, and 10-degree X/Y perturbations.
- **Tropism adaptation**: Eq. 10 developmental bending of branch segments as a module grows; it does not rerun rigid module-orientation search.
- **Plant conduit network**: continuous rooted plant structure shared by diameter, accumulated-light, and vigor calculations. Each module attachment makes the parent terminal and child root one shared conduit junction rather than introducing an edge or physical break.
- **Developed frontier**: leaf points of a module's currently non-zero-length segment structure. At maturity these coincide with the module's terminal nodes.
- **Continuous pipe model**: developmental Eq. 8 calculation over the plant conduit network. A leaf edge's support diameter is terminal thickness; otherwise support diameter is the square root of summed current child-diameter squares. Each edge interpolates from terminal thickness toward that support diameter by segment maturity, including across shared module-attachment junctions, and surviving edges retain their greatest developed diameter after shedding.
- **Plant space**: canonical meter-based coordinates used by plant growth and placement.
- **Prototype geometry scale**: one prototype-library-specific conversion from its arbitrarily authored coordinates into plant space. It is not a universal OBJ import scale; all nine prototypes in the current library share the same scale.

The stateful deep `PlantSimulation` module owns plant architecture, attachment, atomic stepping, and snapshots behind `create`, `step`, and `snapshot`. Ecosystem/population orchestration, rendering, files, and workspace state remain outside. Equations and accepted policies live in `docs/model.md` and `docs/adr`.
