# Plan: repeated attachment

Implementation plan for the attachment half of milestone 5. Delete this file when the work lands; the ADRs and `packages/growth/docs/model.md` carry the decisions afterwards.

## Scope

**In:** generalize attachment from the root-only path to every module that crosses maturity, so generation 2 and beyond attach.

**Out:** shedding, terminal reuse after shedding, re-attachment at terminals that were ineligible when their module matured, senescence.

## Decisions already settled

Do not re-litigate these; they were chosen deliberately.

- **One-shot at crossing.** A module attaches only during the step it crosses maturity. Terminals ineligible then are never revisited. Re-attachment at terminals freed by shedding belongs to the shedding milestone.
- **One combined orientation batch.** When several modules cross in the same step, every new child avoids every other new child, not just its own siblings. Parents are processed in module-record order, which is parent-first creation order and therefore deterministic. The shared avoidance list grows as each child is oriented.
- **Per-parent prototype selection.** `nearest_morphospace_prototype` uses that parent's own vigor for `D'`, and one prototype is reused across that parent's eligible terminals (ADR 0005).

## What the two-scale vigor work already handles

- **A child holds no vigor during the step that creates it.** The terminal-to-child vigor handoff no longer exists; Pass B assigns a child its vigor on the following step. No code is needed to enforce this.
- **New children are excluded from all three passes** while `diagnostics_active` is false, so a parent does not divide vigor among children that do not functionally exist yet.
- **A module intersection with no main-axis child** is handled: Eq. 2 leaves the main branch zero light and the whole flux goes to the lateral group. Repeated attachment reaches this case far more often than root-only attachment does — plant type `i` reaches it on every attachment step.

## Correction: the module-scale terminal seeding does not affect this milestone

Terminal eligibility is seeded by a module's own direct exposure, `q(n_i) = Q(u)/#n`, rather than by accumulated light that includes child subtrees. That change does **not** alter attachment at any generation.

A module evaluates its terminals exactly once, when it crosses maturity, and at that instant it has no children, because children attach only during their parent's own crossing step. Every terminal's child-light term is therefore zero and both seedings agree. Expect no topology change from it here.

It matters in two other places: the `MatureTerminalSnapshot.vigor` reported for a module that already has children, and the shedding milestone, where a mature module would re-evaluate terminals with some occupied and some freed.

## Implementation

All anchors are in `packages/growth/src/plant_simulation.cpp`, whose attachment block currently spans roughly lines 258-320.

1. **Capture per-module pre-commit vigor.** `precommit_root_vigor` (line 241) reads the root's vigor from the pre-integration snapshot. Replace it with a vector filled from that same snapshot; `snapshot_modules_` is in module-record order, so indices align. This vigor is raw and may exceed maximum module vigor, which is correct: `vigor_scaled_determinacy` clamps internally, so `D'` stays within `[0, D]` without clamping at the call site.

2. **Compute a crossed flag per module.** `old_ages` (line 248) already covers every module. Generalize the `root_crossed` predicate (line 262) to `old_ages[i] < fully_grown_age[i] && physiological_age[i] >= fully_grown_age[i]`.

3. **Freeze the pre-attachment module count** before the loop. Iterating only that prefix is what prevents a child attached this step from attaching its own children in the same step.

4. **Rebuild the crossing snapshot once** if any module crossed, and compute plant-wide determinacy and apical control once; both are functions of plant age and identical for every parent.

5. **Loop parents in module-record order.** For each crossed module: select its prototype from its own pre-commit vigor; walk its prototype's ordered terminal nodes; skip terminals that are occupied or whose vigor is at or below minimum module vigor; orient against the shared sphere list seeded at line 242; append each new child's mature sphere to that list so later parents' children avoid it; push the child record with `parent_module_index` set to this parent and the attachment event with its id.

Consider lifting the block into a private `attach_matured_children` method. It is deep enough to justify the header change rather than being a thin wrapper, but confirm before adding it.

## Tests

New:
- A second generation attaches once a first-generation child matures, with a correct parent chain.
- Two modules crossing in the same step orient as one batch: their children avoid each other and placements are deterministic.
- A terminal ineligible at its parent's crossing stays unattached in later steps even if its vigor later rises, locking in the one-shot gate before the shedding milestone can regress it.
- Module ids stay monotonic and are never reused.

Existing to watch: `plant development remains deterministic without grandchildren` chooses fixtures that stop before a second generation. If repeated attachment makes those fixtures produce grandchildren, reconcile the fixture or the name rather than re-baselining silently.

## Verification

`ctest --preset core`, the growth standalone suite, and `cmake --build --preset desktop`, since desktop consumes the snapshot. Milestone 5's own bar for this half is deterministic runs, stable ids, and light and vigor conservation across generations.
