# Single-plant growth model

Complete reference for plant-scale growth of one plant, from Synthetic Silviculture (SS) and its
source Palubicki et al. 2009. Goal: no academic ambiguity remains during implementation. Uses
`CONTEXT.md` terms. Symbols in `backticks` are paper notation.

## Scope

Plant workspace = one plant, no ecosystem. Excluded (ecosystem-only, no decision needed):
temperature/precipitation axes, automatic species selection, seeding/dispersal, successions, gap
dynamics, inter-plant shadow propagation. Plant type is user-selected directly.

Module scale (§5.3, §5.3.1) is **already implemented** (`make_growth_snapshot`). Plant scale (§5.2)
— module architecture, light/vigor distribution, attach/shed, orientation — is **new**.

**First plant milestone = full morphospace**: all nine prototypes with Voronoi selection and `D'`, so
determinacy is active from the start (no single-prototype bypass). This runs on the current 2D
prototypes: module orientation at attachment (§5.2.3) rotates each module in `φ`/`ψ` while
`f_collisions` pushes modules out of plane, so the whole-tree architecture is 3D even with flat
prototypes. Prototype depth is an additional source of 3D form (out-of-plane branches within each
module) that we simply haven't authored yet — a realism improvement, not a functional prerequisite.

## Plant-scale loop (per simulation step)

```
1. Light      Q(u) = exp(−f_collisions(u))                      eq1
2. Vigor      two-pass Borchert-Honda over the module tree      eq2
3. Growth     Υ(u) = smoothstep((v̄−v̄min)/(v̄max−v̄min))·g_p       eq5  (module scale, done)
4. Age        da_u/dt = Υ(u), Euler, 0 → a_mature               eq6  (module scale, done)
5. Attach/shed/senesce
```

## Equations

Plant scale (new):

```
f_collisions(u) = Σ_w V_intersect(B_u, B_w)          eq1   self-collisions only (single plant)
Q(u)            = exp(−f_collisions(u))              eq1
Q(u)            = Q(u_m) + Q(u_l)                    basipetal (tips→root)
v̄(u_m)          = v̄(u)·λQ(u_m) / (λQ(u_m)+(1−λ)Q(u_l))   eq2   acropetal (root→tips)
v̄(u_l)          = v̄(u) − v̄(u_m)                      eq2
```
`λ>0.5` excurrent (trunk), `≤0.5` decurrent (spreading). Root allocation and shedding: see Decisions.

Module selection (§5.2.2): new module placed in morphospace at `(λ, D')`, `D' = v̄(u_parent)·D/v̄_max`;
Voronoi over 9 prototype seeds picks the prototype = nearest seed to the query point (for the even 3×3
grid, snap `λ` and `D'` each to the nearest of {0.17, 0.5, 0.83}; no Voronoi diagram needed). `λ` picks
the row (fixed per plant type), vigor picks the column via `D'`. See Morphospace.

Module orientation (§5.2.3 / A.1): discrete coordinate descent on Euler angles `ρ=[φ,θ,ψ]`.
Start = parent module orientation. Each step tries `P = {[α_s,0,0],[−α_s,0,0],[0,0,α_s],[0,0,−α_s]}`
(perturb `φ` and `ψ` only — `θ` never changes) and keeps the candidate with lowest `f_distribution`;
stop after a few steps or `f_distribution < error`.

```
f_distribution(u) = ω₁·f_collisions(u) + ω₂·f_tropism(u)     eq3
f_tropism(u_α)    = ‖cos(α_tropism) − cos(u_α)‖              eq4
```

Module scale (already implemented, listed for completeness):

```
a_b = max(0, a_u − a_n)                              eq7   segment age
d_b = √(Σ_{c∈C_b} d_c²)  else φ                      eq8   pipe model, n=2
ℓ_b = min(ℓ_max, β·a_b)                              eq9   segment length
τ_offset(a_b) = (g₁·ĝ_dir·g₂) / (a_b + g₁)           eq10  per-segment tropism bend
```
eq10: `ĝ_dir` = normalized gravity; sign of the strength selects gravitropism (−) / phototropism (+);
offset decays as segment age `a_b` grows. Added to node positions.

## Paper → PlantTypeParameters (Table 4, plant types a–p)

| Table 4 | struct field | role |
|---|---|---|
| `p_max` | `plant_max_age` | senescence onset |
| `v̄_rootmax` | `root_max_vigor` | root vigor (constant) |
| `g_p` | `plant_growth_rate` | scales Υ (eq5) |
| `λ/λ_mature` | `apical_control` / `mature_apical_control` | eq2 split + morphospace axis |
| `D/D_mature` | `determinacy` / `mature_determinacy` | morphospace axis (selection only) |
| `F_age` | `flowering_age` | when mature params apply (`-` = never) |
| `α` | `tropism_angle` | eq4 tropism target angle |
| `ω₂` | `tropism_weight` | eq3 orientation weight |
| `g₁` (signed) | `tropism_strength` | eq10 tropism strength+direction |
| `φ` | `terminal_thickness` | eq8 terminal diameter |
| `β` | `length_growth_scale` | eq9 length scale |

The struct matches Table 4 exactly; nothing missing for single-plant.

## Morphospace (resolved — ADR-0013)

Nine prototypes `Cube`..`Cube.008` on an even 3×3 grid at unit-square cell centers.
**D increases left→right (X); λ increases bottom→top (Y).** Traced from SS Fig. of §5.2.2.

```
 λ hi | Cube.006  Cube.007  Cube.008
 λ mid| Cube.003  Cube.004  Cube.005
 λ lo | Cube      Cube.001  Cube.002
        D lo      D mid     D hi
```

Seed coordinates `(λ, D)`, levels ∈ {0.17, 0.5, 0.83}:

| proto | λ | D | proto | λ | D | proto | λ | D |
|---|---|---|---|---|---|---|---|---|
| Cube | .17 | .17 | Cube.001 | .17 | .5 | Cube.002 | .17 | .83 |
| Cube.003 | .5 | .17 | Cube.004 | .5 | .5 | Cube.005 | .5 | .83 |
| Cube.006 | .83 | .17 | Cube.007 | .83 | .5 | Cube.008 | .83 | .83 |

**Selecting a prototype = pick the nearest seed** to the query point `(λ, D')`. "Voronoi" is just the
paper's name for that nearest-seed partition — needed because *their* seeds are scattered arbitrarily.
On our even grid it reduces to snapping `λ` and `D'` each to the nearest of {0.17, 0.5, 0.83}: two
roundings, no diagram, no library. `λ` (fixed per plant type) picks the row; vigor via `D'` picks the
column.

Prototypes are currently 2D tracings (z≈0). Sufficient for the milestone: each module stays planar but
the architecture still spreads in 3D via module orientation. Prototype depth is an additional (not the
only) source of 3D form — it adds out-of-plane branches *within* a module. Realism improvement,
optional.

## Decisions & provisional rules

Every unspecified item has an explicit rule so no academic decision is needed mid-implementation.

- **Light (single plant)** — self-collision only; unoccluded sky. No inter-plant shadow. (ADR-0012)
- **Root vigor** — root receives constant `v̄_rootmax` each step (paper: "never allocate more than
  `v̄_rootmax`"; senescence text confirms it is the per-step allocation). No `α` scaling — Table 4's
  `α` is the tropism angle, not a vigor coefficient.
- **`v̄_max = v̄_rootmax`** — the root holds the maximum (matches `VigorInputs::max_for`).
- **Shedding** — module removed when `v̄ < v̄_min`.
- **Senescence** — at `p_t ≥ p_max`, interpolate `v̄_rootmax → 0` at constant step until all modules shed.
- **`B_u`** — sphere centered at module-geometry centroid; diameter = module tip-to-tip extent
  (max pairwise node distance).
- **Node age `a_n`** — provisional path-length / `β` until authored into prototypes. (ADR-0006)
- **Borchert-Honda beyond binary** — eq2 is main + one lateral; for a node with >2 children, apply
  eq2 recursively (main vs. the combined rest). Our interpretation of an unstated case.
- **Root module prototype** — the root has no parent, so `D'` is undefined. Select its prototype as if
  the seed were full vigor (`D' = D`), i.e. the nearest seed to `(λ, D)`. The paper doesn't specify this.
- **Orientation** — only `ω₂` is tabulated, so fix `ω₁ = 1`; step angle `α_s` small and step count
  "few or until `f < error`" are our tuning. Note `α_s` (A.1 step size) ≠ `α_tropism` (eq4) — the
  paper reuses `α`.
- **Tropism naming** — the paper's eq10 names `g₁` (time decay) and `g₂` (signed strength), but
  Table 4 tabulates a single signed column labelled `g₁`. Treat the tabulated signed value as
  strength+direction (`tropism_strength`); segment-age falloff is handled by `tropism_falloff_age`.
  The paper's `g₁`/`g₂` labelling is internally inconsistent — flagged, not guessed.

## Sources

- Synthetic Silviculture (Makowski et al. 2019) — §5.2, §5.3, §5.3.1, App. A.1, Table 4 (Fig. 21).
- Self-organizing tree models (Palubicki et al. 2009) — §3 (main/lateral topology), §4.2 (Borchert-Honda).
