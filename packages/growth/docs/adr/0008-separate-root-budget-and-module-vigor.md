---
status: accepted
---

# Bound maximum module vigor at the growth rate, not the vigor flux

`v̄_rootmax` is the Table 4 cap on the vigor allocated to the root module; `v̄_max` is the shared per-module vigor scale and equals `1`. Synthetic Silviculture uses distinct notation and does not list `v̄_max` as a plant-type parameter.

The plant carries one vigor quantity. `v̄(u_root) = min(Q_accumulated(u_root), v̄_rootmax)`, and Eq. 2 divides it among child modules without loss, so a module's vigor may exceed `v̄_max`. The paper's instruction to clamp `v̄(u)` to `v̄_max` is a normalization, applied only where a bounded value is required: Eq. 5, whose sigmoid argument `(v̄ − v̄_min)/(v̄_max − v̄_min)` must lie in `[0, 1]`, and morphospace `D′ = v̄(u_parent) · D / v̄_max`, which only maps into `[0, D]` when its numerator is bounded. Truncating the propagated flux instead would discard vigor at every module junction and make `v̄_rootmax` unreachable.

`v̄_max` keeps the value `1`. The paper supplies no number, but every module's direct exposure `Q(u) = exp(−f_collisions(u))` is at most `1`, making unit scale the natural per-module reference; equating `v̄_max` with `v̄_rootmax` instead makes an unshaded root grow at roughly `0.0016 · g_p` and never mature.
