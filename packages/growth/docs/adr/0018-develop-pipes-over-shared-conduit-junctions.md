---
status: accepted
---

# Develop pipes over shared conduit junctions

Treat each module attachment as one shared parent-terminal/child-root junction in the plant conduit network. For each edge, support diameter is terminal thickness at a leaf or the square root of summed current child-diameter squares; interpolate from terminal thickness toward that support by segment maturity, then retain the greatest diameter reached by each surviving edge. This keeps mature structure consistent with Eq. 8, avoids visible jumps during development or upstream shrinkage after shedding, and removes module-boundary pipe rules without persistent target-diameter state; the cost is one persistent diameter per edge.
