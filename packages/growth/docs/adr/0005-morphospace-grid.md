---
status: accepted
---

# Use an even 3x3 morphospace grid

Nine prototypes occupy levels `{1/6, 1/2, 5/6}` with determinacy increasing across columns and apical control across rows. Selection chooses the nearest seed to `(apical control, vigor-scaled determinacy)`. For every child attached by one mature parent in a plant step, vigor-scaled determinacy uses that parent module's vigor and the selected prototype is reused across its eligible terminals; terminal vigor controls attachment eligibility, not prototype choice.

`D′ = v̄(u_parent) · D / v̄_max` maps into `[0, D]` only while its numerator is bounded, so the parent's vigor is clamped to `v̄_max` for this query alone; the propagated vigor flux stays unclamped.
