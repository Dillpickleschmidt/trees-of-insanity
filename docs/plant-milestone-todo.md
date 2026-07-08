# Milestone: single-plant growth previews

**Goal:** in the Plant workspace, select any built-in species (one of 16 plant type presets `a`–`p`),
scrub a plant-development slider, and watch the whole plant grow via the documented plant-scale model,
rendered in the existing ovrtx viewport. Ship = full-plant previews for each built-in species.

**Reference:** [`single-plant-growth-model.md`](single-plant-growth-model.md) (equations, Table 4 mapping,
morphospace 3×3 grid, provisional rules). HTML plans were authored in a temp dir for review; build steps
below distill them. Honor ADRs 0006, 0007, 0008, 0009, 0012, 0013 — do not re-litigate.

**Sequencing:** 1 → 2 → 3 → 4. Each piece depends on the prior's output type. All paths are relative to
`~/Projects/trees-of-insanity/`.

**Decisions already made** (from grilling; don't re-open unless a hard blocker appears):
- `develop_plant(age)` is **pure + deterministic** — re-develops from age 0 each call (mirrors
  `make_growth_snapshot(age)`). No stepping simulation unless perf forces it.
- Root module prototype selected as if full vigor (`D' = D`).
- Borchert-Honda >2 children: apply eq2 recursively (main vs. combined rest).
- Orientation: `ω₁ = 1` fixed, `ω₂` from Table 4; small step angle, few steps.
- Species gallery previews built-ins via a transient `plant.preview_preset` command (no plant-type-library churn).

---

## Piece 1 — Plant development module (`toi::plant`)

The deep core. Interface: `develop_plant(plant_type, prototype_library, plant_age) → PlantArchitecture`.

- [x] Create `src/toi/plant/include/toi/plant/plant.hpp` + `src/toi/plant/src/plant.cpp`; add to CMake `core` preset (no ovrtx/Vulkan).
- [x] Define `PlantArchitecture` = placed modules `{prototype id, world transform, module physiological age, per-module GrowthSnapshot, vigor}` (vigor feeds piece 3's summary).
- [x] Prepare prototypes internally via `prepare_branch_module_prototype(raw, plant_type)` (reuse `toi::growth`).
- [x] Light: `Q(u) = exp(−f_collisions(u))`, self-collision only over own module bounding spheres (`B_u` = centroid, diameter = tip-to-tip extent). (ADR-0012)
- [x] Vigor: basipetal `Q(u)=Q(u_m)+Q(u_l)`; acropetal eq2 split by `λ`, `v̄(u_l)=v̄(u)−v̄(u_m)`; root gets constant `v̄_rootmax`.
- [x] Growth rate eq5 + age eq6 per module by reusing `make_growth_snapshot`.
- [x] Attach: for `a_u > a_mature`, `q(n_i)=Q(u)/#n`, per-terminal-node BH, attach child where `v > v̄_min`.
- [x] Internal seam `select_prototype` — nearest-seed on the 3×3 morphospace grid, `D' = v̄(parent)·D/v̄_max` (root: `D'=D`). Keep private (hypothetical seam). (ADR-0013)
- [x] Internal seam `orient_module` — coordinate descent over `{±φ, ±ψ}` minimizing `f_distribution = ω₁·f_collisions + ω₂·f_tropism`. Keep private.
- [x] Shedding (`v̄ < v̄_min`) and senescence (`p_t ≥ p_max` → ramp `v̄_rootmax → 0`).
- [x] Maturation: swap to `mature_apical_control` / `mature_determinacy` at `p_t ≥ flowering_age`.
- [x] Tests through `develop_plant`: determinism; **all 16 presets develop to a bounded plant (no explosion)**; shedding + senescence fire; vigor conserved at splits.

## Piece 2 — Plant preview projection (`toi::render`)

Interface: `make_plant_preview_stage_projection(architecture, options) → GrowthPreviewStageProjection`
(same output type as the module preview, so the ovrtx viewport seam is unchanged; `PlantArchitecture` now
carries its prepared prototypes, so no separate library argument is needed).

- [x] Factor the existing single-module mesh builder to take `(transform, snapshot)`; call it per placed module.
- [x] Accumulate chains/meshes across all placed modules; resolve continuation topology via prototype ids.
- [x] Internal seam `camera_from_bounds(Bounds)` — whole-plant framing camera (real seam: also used by module preview).
- [ ] Optional `options.camera_bounds` framing hint to avoid camera jitter across scrub ticks. _(Deferred: camera frames current bounds each projection; stable-framing policy decided in Piece 3/4 once scrub UX is evaluated.)_
- [x] Honor ADR-0007 (growth separate from render), ADR-0008 (chain meshes). Cross-module diameter continuity (eq8) is piece 1's job, not render's.
- [x] Tests: a 2-module architecture yields >1 chain; camera frames all modules; module set tracks age.

## Piece 3 — Plant workspace surface (`toi::app`)

Controller as thin conductor over pieces 1–2; controller stays the single command seam.

- [x] `ApplicationController`: add `plant_architecture()`, `plant_growth_snapshot_summary()`, `plant_preview_stage_projection()`, `set_plant_physiological_age(float)`, `plant_fully_grown_age()`.
- [x] Add `plant_physiological_age_` field (mirrors `module_physiological_age_`) and `active_workspace_` field.
- [x] Develop the plant **once per command handler** (mirror `inspect.snapshot`).
- [x] `CommandMap` additions (`src/shared/appCommands.ts` + C++ `application_commands.cpp`): `plant.set_age`, `plant.get_growth_snapshot_summary`, `plant.get_growth_preview_stage`, `plant.get_architecture_summary`, `workspace.set`, `plant.preview_preset`.
- [x] Add the plant preview-changing commands to `application_command_changes_preview(method)` so the native viewport re-projects.
- [x] `state()` reports `active_workspace = "plant"` and marks the plant `workspace_previews` entry `implemented=true`.
- [x] Species = existing `active_plant_type` (reuse `set_active_plant_type`).
- [x] Tests: `set_plant_physiological_age` drives `plant_preview_stage_projection`; switching active plant type re-develops.

## Piece 4 — Plant workspace + species gallery (Solid UI)

Mirror the Module workspace; reuse the native `Viewport` (one adapter, both workspaces).

- [x] Lift shared structure from the Module workspace; add `PlantWorkspace` pane in `src/mainview/` (plant type selector, plant development slider, growth summary, reused `Viewport`).
- [x] Species gallery iterating the 16 preset keys `a`–`p`; a click instantiates that species (`plant_types.create` + `plant.set_active_plant_type`, deduped by name) so the viewport renders it. _(v1 uses instantiate-and-activate; `plant.preview_preset` stays available for a future in-viewport transient preview.)_
- [x] Wire slider → `appClient.command("plant.set_age", {age})`; native viewport re-renders on the changes-preview predicate (no pixel fetching).
- [x] Unlock the plant workspace preview in `TopBar` once piece 3 marks it implemented.
- [x] Use solidcn Select/Slider (ADR-0009).
- [x] Verified via typecheck + `build:ui` + the Piece 3 command-seam tests (`workspace.set`, `plant.set_age`, preset instantiation). _(No Solid component-test harness in the repo; UI wiring is typed against the CommandMap. On-screen scrub of all 16 species is the deferred manual check below — needs the ovrtx build.)_

---

## Validation (run the relevant subset per piece; all from repo root)

- [x] `bun run typecheck`
- [x] `bun run build:ui`
- [x] `cmake --preset core && ctest --preset core` — 22/22 (pieces 1–3 + render)
- [ ] `bun run build:native` (ovrtx path) — _not runnable here: `TOI_ENABLE_OVRTX` needs the ovrtx package; native_core's plant dispatch is verified by inspection._
- [ ] `bun run verify:shell` — _deferred: needs the ovrtx viewport running._
- [ ] Manual: pick each of the 16 species, scrub the slider full range, confirm a bounded plant grows with no flicker. — _deferred: needs the on-screen ovrtx build._

## Definition of done

- [x] All four pieces merged; `toi::plant` tested through `develop_plant`.
- [x] Every built-in species (`a`–`p`) develops to a bounded, deterministic multi-module plant and projects to renderable chains (tested); the on-screen scrub is the deferred manual check above.
- [x] `CONTEXT.md` carries the plant-scale terms; ADRs 0012 (self-collision light), 0013 (morphospace grid), 0014 (simulation provisional choices) record the load-bearing decisions.
