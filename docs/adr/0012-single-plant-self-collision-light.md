---
status: accepted
---

# Single-plant light uses intra-plant self-collision only

In the plant workspace (one plant, no ecosystem), module light exposure is computed only from
intersections among that plant's own module bounding volumes: `Q(u) = exp(−f_collisions(u))`, with
`f_collisions` summing `V_intersect(B_u, B_w)` over the same plant's modules (SS eq1). The sky is
otherwise unoccluded. Inter-plant shadow propagation (SS ecosystem scale, Palubicki 2009 §4.1) is out
of scope.

This is faithful to SS §5.2.1, which uses bounding-volume self-collision at plant scale. Self-shading
still regulates crown density and drives shedding, so an isolated plant self-organizes rather than
growing unbounded.

## Consequences

- Uniform `Q=1` was rejected: it removes light competition, so form would depend only on apical
  control and tropism, losing crown density regulation and self-pruning.
- Shadow-propagation self-shading is deferred; it is the heavier ecosystem-scale mechanism.
- Reintroducing neighbors later only widens the `f_collisions` sum over more modules — no model change.
