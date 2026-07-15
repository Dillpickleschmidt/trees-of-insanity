---
status: accepted
---

# Use meter-based plant space and raw collision volumes

Plant growth, placement, and module development use meters consistently, matching the USD stage declaration `metersPerUnit = 1`. Table 4 terminal thickness `φ` and length-growth coefficient `β` are interpreted as centimeter-valued structural parameters and converted to meters once at the parameter boundary. Arbitrary prototype coordinates are converted separately by a prototype-library geometry scale; the current nine-prototype library uses one shared `2×` scale.

Synthetic Silviculture Eq. 1 uses raw bounding-sphere intersection volumes in cubic meters before `Q(u) = exp(-f_collisions(u))`; it does not divide by the subject module volume. Each module compares against every other module except itself, including directly attached parents and children, because the paper defines no adjacency exclusion. A module's sphere encloses its currently developed points: its center is the midpoint of their module-local axis-aligned bounds and its radius reaches the furthest current point. The rigid module transform places this sphere in plant space, preserving its size across orientations. The center therefore depends on spatial extent rather than the number of branch endpoints. This is one global growth-domain unit model, independent of any workspace or other consumer.
