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
- **Maximum module vigor**: shared per-module cap `v̄_max = 1`; it is distinct from a plant type's maximum root vigor budget.
- **Minimum module vigor**: provisional shared cutoff `v̄_min = 0.02` used by growth, attachment, and shedding until whole-plant calibration is possible.
- **Shedding**: removal of a below-minimum-vigor module and its attached descendant subtree before surviving modules grow or attach children in that plant step.
- **Root vigor budget**: total vigor entering the acropetal pass, equal to root accumulated light capped by the plant type's maximum root vigor.
- **Maximum root vigor**: plant type's Table 4 cap `v̄_rootmax` on the root vigor budget.
- **Direct light exposure**: one module's available light/space estimate `Q(u)`, derived from raw bounding-sphere intersections with every other module except itself.
- **Accumulated light**: a module's direct light exposure plus the accumulated light of all its attached child subtrees. Mature modules route this through terminal and branch topology; an immature leaf module's accumulated light equals its direct light exposure.
- **Module bounding sphere**: sphere enclosing a module's currently developed geometry, centered at that geometry's centroid with radius equal to its furthest point.
- **Basipetal pass**: tip-to-root accumulated-light calculation described by the papers, not currently orchestrated.
- **Acropetal pass**: root-to-tip vigor distribution through the same continuous module/terminal topology used by accumulated light.
- **Morphospace**: fixed apical-control/determinacy grid used by the pure selection policy.
- **Main axis**: precomputed dominant continuation through a prototype fork. Children within 10 degrees of the straightest continuation are treated as equivalently aligned, then resolved by pipe-diameter factor, exact alignment, longest downstream path, and stable segment order.
- **Lateral group**: all non-main children at one fork, treated as one side of the Borchert-Honda split before their shared vigor is divided proportionally by accumulated light.
- **Module orientation**: rigid transform selected once when a new module attaches, starting from its parent module's orientation and evaluating the new module's mature prototype extent. Collision weight is the fixed baseline `ω₁ = 1`; plant-type `ω₂` controls tropism relative to it. Search uses at most three iterations with a provisional 10-degree perturbation.
- **Tropism adaptation**: Eq. 10 developmental bending of branch segments as a module grows; it does not rerun rigid module-orientation search.
- **Continuous pipe model**: basipetal Eq. 8 diameter calculation crossing module attachments, where the child's currently developed root diameter supplies its parent terminal and contributes toward the plant root.
- **Plant space**: canonical meter-based coordinates used by plant growth and placement.
- **Prototype geometry scale**: one prototype-library-specific conversion from its arbitrarily authored coordinates into plant space. It is not a universal OBJ import scale; all nine prototypes in the current library share the same scale.

The stateful deep `PlantSimulation` module owns plant architecture, attachment, atomic stepping, and snapshots behind `create`, `step`, and `snapshot`. Ecosystem/population orchestration, rendering, files, and workspace state remain outside. Equations and accepted policies live in `docs/model.md` and `docs/adr`.
