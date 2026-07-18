---
status: accepted
---

# Accumulated light includes every module's direct exposure

The basipetal pass defines a module's accumulated light as its own direct light exposure plus the accumulated light of every child. Synthetic Silviculture states that each module has light exposure but only shows child-axis addition at a branching point; including each module's direct exposure ensures no internal module's measured exposure disappears and every module contributes exactly once.

This is provisional. Read literally, the paper's `Q(u) = Q(u_m) + Q(u_l)` collects light only from tip modules, which contradicts every module having a bounding volume and an exposure. Revisit before release, or sooner if plant form suggests internal modules are over-contributing.
