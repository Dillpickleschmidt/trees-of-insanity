---
status: accepted
---

# Use render chain meshes for Growth preview

Growth preview renders branch module instances as generated render chain meshes. A render chain follows continuation segments and welds their rings into one mesh; lateral segments start separate render chains from the same centerline node.

## Consequences

- Growth facts stay separate from renderer geometry.
- ovrtx receives real mesh geometry for taper, caps, UVs, and future bark materials.
- The Growth preview can keep stable topology while module physiological age changes update mesh points in batches.
- Basis curves and per-segment debug lines are not the main Growth preview representation.
