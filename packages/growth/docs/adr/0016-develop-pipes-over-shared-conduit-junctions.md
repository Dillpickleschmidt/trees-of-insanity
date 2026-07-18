---
status: accepted
---

# Develop pipes over shared conduit junctions

The plant conduit network spans module branch topology and terminal attachments as one rooted tree, because a module boundary is not a physical pipe break. Only the Eq. 8 pipe model uses it; light and vigor are computed at module scale instead, per `0017-two-scale-light-and-vigor.md`.

Treat each module attachment as one shared parent-terminal/child-root junction in that network. For each edge, support diameter is terminal thickness at a leaf or the square root of summed current child-diameter squares, so a child contributes only the diameter it has actually developed and never its eventual target; interpolate from terminal thickness toward that support by segment maturity, then retain the greatest diameter reached by each surviving edge, so shedding a subtree removes its contribution without shrinking the pipe that survived it.

This keeps mature structure consistent with Eq. 8, avoids visible jumps during development or upstream shrinkage after shedding, and removes module-boundary pipe rules without persistent target-diameter state. Prototype topology stays precomputed while dynamic diameters traverse attachments; the cost is one persistent diameter per edge.
