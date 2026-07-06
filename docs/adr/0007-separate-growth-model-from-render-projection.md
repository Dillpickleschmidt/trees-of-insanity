---
status: accepted
---

# Keep the growth model separate from render projection

The growth model is not represented as one renderer object per plant, branch module, branch node, or branch segment. Rendering consumes a render projection: visible, LOD-filtered, batched state suitable for ovrtx and GPU presentation.

## Consequences

- Growth snapshot data and render projection data are separate interfaces.
- ovrtx receives generated render state, not the Project or ecosystem model.
- This preserves a path to ecosystem-scale populations without constraining growth concepts to renderer scene-graph granularity.
