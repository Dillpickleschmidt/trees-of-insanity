# Growth library context

This package implements renderer-independent plant development following Synthetic Silviculture.

## Language

- **Branch module prototype**: reusable branch-node/segment topology and developmental attributes.
- **Branch module**: one simulated occurrence of a prototype.
- **Module physiological age**: developmental state `a_u`, not calendar time.
- **Growth snapshot**: visible module state at a physiological age.
- **Plant type**: growth parameter set; never call it a species in library code.
- **Module architecture**: ordered tree of connected branch modules forming one plant.
- **Plant**: one simulated organism governed by a plant type.
- **Plant age**: developmental counter `p_t`.
- **Vigor**: module growth potential `v̄(u)`.
- **Module light exposure**: available light/space estimate `Q(u)`.
- **Basipetal pass**: tip-to-root light accumulation.
- **Acropetal pass**: root-to-tip vigor distribution.
- **Morphospace**: apical-control/determinacy parameter space selecting prototypes.
- **Continuation segment**: child preserving incoming direction.
- **Lateral segment**: non-continuation child.

The complete equations and terminology live in `docs/model.md`.
