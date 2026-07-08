---
status: accepted
---

# Morphospace prototype coordinates use an even 3×3 grid

Synthetic Silviculture positions nine branch module prototypes in a morphospace spanned by apical
control `λ` and determinacy `D`, and selects a prototype by Voronoi region. The paper states the nine
positions are "chosen arbitrarily," so the layout is a design decision, not paper-given.

We fix the nine prototypes `Cube`..`Cube.008` to an even 3×3 grid at unit-square cell centers
(levels {0.17, 0.5, 0.83}), with **determinacy D increasing left→right (X)** and **apical control λ
increasing bottom→top (Y)**. This matches the traced prototype set, laid out row-major from
bottom-left, and the ordering in the SS §5.2.2 inset figure (right column Cube.002/005/008 highest D;
top row Cube.006/007/008 highest λ). Full seed table in `single-plant-growth-model.md`.

## Consequences

- The first plant milestone uses the full morphospace: all nine prototypes with Voronoi selection and
  `D'`, so determinacy is active from the start. No single-prototype bypass.
- Prototypes must be authored to these morphospace positions. The current 2D OBJ tracings are
  sufficient — module orientation at attachment already makes the whole-tree architecture 3D. Prototype
  depth is an additional (not the only) source of 3D form, adding out-of-plane branches within a
  module; authoring it is an optional realism improvement.
- `D' = v̄(u_parent)·D / v̄_max` shifts selection toward higher-D prototypes for vigorous parents.
