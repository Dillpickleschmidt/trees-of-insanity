# Plan: repeated attachment

Implementation plan for the attachment half of milestone 5. Delete this file after implementation; retain durable behavior in the growth ADRs and `packages/growth/docs/model.md`.

## Scope

**In:** attachment for every module that crosses maturity, including generation 2 and later.

**Out:** shedding, terminal reuse after shedding, later attachment at terminals that were ineligible during maturity crossing, and senescence.

## Required behavior

- A module may attach children only during the step in which it crosses maturity.
- Only modules present at the start of the attachment phase may act as parents. A child cannot attach its own children in its creation step.
- Each crossed parent selects one morphospace prototype from its own pre-integration vigor and uses it for every eligible terminal.
- Crossed parents are processed in module-record order. Their eligible terminals use the prototype's prepared orientation order.
- All children created in one step share one orientation batch. Each selected mature child sphere participates in every later orientation in that batch, including orientations for another parent.
- An attachment requires an unoccupied terminal with module-scale terminal vigor above `kMinimumModuleVigor`.
- New module IDs increase monotonically, and every child record and attachment event identifies the correct parent module and terminal.

## Implementation

Implement the attachment batch in `PlantSimulation::step` in `packages/growth/src/plant_simulation.cpp`.

1. After the initial `rebuild_snapshot()`, save `module_records_.size()` as the attachment cohort size. Build a pre-integration vigor vector from `snapshot_modules_`; snapshot and record indices share module-record order. Keep each value raw because `vigor_scaled_determinacy` performs the morphospace-only clamp.

2. Capture each cohort module's old physiological age, integrate its new age, and record whether it crossed maturity:

   ```text
   old_age < fully_grown_age && physiological_age >= fully_grown_age
   ```

3. Seed one orientation-sphere vector from the initial snapshot's module spheres. Keep this vector for the complete attachment batch.

4. If at least one module crossed, call `rebuild_snapshot()` once to produce mature terminal positions, tangents, occupancy, and vigor. Compute determinacy and apical control once for the step.

5. Iterate crossed parent indices from zero to the saved cohort size. Copy the parent's ID, prototype index, and transform before appending children so `module_records_` reallocation cannot invalidate parent state.

6. For each parent:
   - Select one prototype with `nearest_morphospace_prototype(apical_control, vigor_scaled_determinacy(determinacy, preintegration_vigor[parent_index]))`.
   - Compute the selected prototype's fully grown age once.
   - Visit the parent's terminal nodes in `prototype_orientation_data_` order.
   - Find the corresponding `MatureTerminalSnapshot` by parent ID and terminal node.
   - Skip missing, occupied, or below-threshold terminals.
   - Orient the child against the shared orientation-sphere vector.
   - Append a child record with the parent index, terminal node, age zero, and `diagnostics_active = false`.
   - Append the attachment event using the parent and child IDs.
   - Append the child's mature sphere to the shared orientation batch.

Keep attachment as one batch operation. If extracted, a private helper should own the crossed-parent loop, prototype selection, terminal filtering, orientation, and commit rather than wrap individual appends.

## Tests

- A first-generation module attaches children when it crosses maturity, producing a correct multi-generation parent chain.
- A module produces attachments only on its crossing step; later steps do not duplicate them.
- A terminal below the vigor threshold during crossing remains unattached before shedding, even if its vigor later rises.
- Parents crossing together select prototypes from their respective vigor values.
- Children of parents crossing together use one deterministic orientation batch and avoid one another.
- IDs remain monotonic across generations, and attachment events contain the correct parent IDs and terminal nodes.
- Light and vigor remain conserved across multiple generations.
- Repeated plant development remains deterministic across multiple generations.

## Verification

- `ctest --preset core`
- Configure, build, and run the standalone `packages/growth` test suite.
- `cmake --build --preset desktop`

The attachment half is complete when repeated generations are deterministic, IDs and parent chains remain stable, and light and vigor are conserved.
