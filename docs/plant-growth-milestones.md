# Single-plant growth milestones

Implement the accepted Synthetic Silviculture model through small human-verifiable slices. Growth behavior belongs to the deep renderer-independent `toi::growth::PlantSimulation`; desktop owns Project/workspace state and projections only.

## 1. Metric foundation

- Convert the current prototype library with its shared `2×` geometry scale into meter-based plant space.
- Convert Table 4 `φ` and `β` centimeter values to meters.
- Keep USD `metersPerUnit = 1`.
- Use raw cubic-meter Eq. 1 collision volumes; remove own-volume normalization.
- Preserve Module-workspace visual proportions and fully-grown physiological ages.

Verify Module appearance/timing and the gradual three-module collision fixture.

## 2. Project/workspace state

- Replace the unreleased Project schema directly; no migration or fallback.
- Persist project-wide authored content and complete typed Module, Plant, and Ecosystem workspace states.
- Give each workspace independent viewport state.
- Remove the separate viewport-preferences file.

Verify existing Module behavior and persistence before enabling Plant.

## 3. Root-only Plant slice

- Add stateful `PlantSimulation` with `create`, `step`, and zero-copy `snapshot`.
- Create one automatically selected age-zero root at world origin.
- Compute direct light exposure, accumulated light, root/module vigor, growth rate, and physiological age.
- Enable Plant with Reset, target-age Run/Stop controls, editable step size, stable camera, root label, and direct-light sphere.

Do not add topology growth in this slice; a mature root remains mature.

## 4. Continuous flow and attachment

- Precompute main-axis continuations and grouped lateral allocation.
- Route light and vigor through one continuous module/terminal network.
- Attach all eligible terminals atomically using parent-level prototype selection.
- Orient new modules once, continue the pipe model across attachments, and expose mature-terminal markers.
- Add depth-aware animated accumulated-light and vigor branch-surface paths.

Verify the first attached generation before long runs.

## 5. Repeated growth and shedding

- Allow descendants to mature and every mature never-used terminal to retry attachment.
- Shed below-threshold modules and descendant subtrees before growth.
- Leave terminals spent after their first attached module; study reattachment separately.
- Keep provisional `v̄_min = 0.02`; the finite-horizon sweep did not identify a preferable global increase, so revisit it with senescence acceptance targets.

Verified deterministic runs, stable non-reused IDs, retained survivor pipes, light/vigor conservation, empty root death, and single-use terminals across bundled plant types.

## 6. Senescence

Implement and calibrate the paper's linear post-`p_max` root-vigor decline only after healthy growth is trusted.

## 7. Ecosystem readiness

Replace private deterministic all-pairs sphere evaluation with an exact-result spatial broad phase before Ecosystem implementation. Do not change the `PlantSimulation` interface or Plant consumers.

## Provisional calibration values

- Minimum module vigor: `0.02`.
- Senescence decline rate: unresolved.

Revisit only with whole-plant evidence; do not add per-workspace or legacy behavior.
