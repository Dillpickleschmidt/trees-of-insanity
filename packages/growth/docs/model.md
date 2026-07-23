# Growth model and equation library

Reference for the retained Synthetic Silviculture math and its source model in Palubicki et al. 2009. The library develops branch modules and simulates arbitrary-depth repeated attachment.

## Module development

Implemented by `growth_rate`, `prepare_branch_module_prototype`, `make_growth_snapshot`, and `fully_grown_age`:

```text
Υ(u) = S((v̄(u) - v̄_min) / (v̄_max - v̄_min)) · g_p    Eq. 5
S(x) = 3x² - 2x³
da_u/dt = Υ(u)                                             Eq. 6
a_b = max(0, a_u - a_n)                                  Eq. 7
d_b = sqrt(Σ d_c²), terminal d_b = φ                     Eq. 8
ℓ_b = min(ℓ_max, β · a_b)                                 Eq. 9
τ_offset(a_b) = g₁ · ĝ_dir · g₂ / (a_b + g₁)             Eq. 10
```

`physiological_age_euler_step` preserves the paper's forward-Euler approximation of Eq. 6 without choosing a clock or timestep. Node age `a_n` remains provisionally derived from scaled root-to-node path length (ADR 0003). Existing module behavior is unchanged.

## Plant-scale equation functions

These pure functions preserve the paper math without architecture or stepping policy.

### Light and vigor

```text
f_collisions(u) = Σ_w V_intersect(B_u, B_w)               Eq. 1
Q(u) = exp(-f_collisions(u))
Q_accumulated(u) = Q_direct(u) + Σ_c Q_accumulated(c)      basipetal accumulation
v̄(u_root) = min(Q_accumulated(u_root), v̄_rootmax)
v̄(u_m) = v̄(u) · λQ(u_m) / (λQ(u_m) + (1-λ)Q(u_l))       Eq. 2
v̄(u_l) = v̄(u) - v̄(u_m)
```

- `collision_measure` calculates Eq. 1's raw cubic-meter intersection sum over every other module; see ADR 0006.
- `light_exposure` calculates direct light exposure `Q(u)`.
- Accumulated light includes each module's direct exposure exactly once; see ADR 0007.
- Light and vigor are computed at the paper's two scales (ADR 0017). At plant scale, accumulated light sums over the module tree and Eq. 2 divides `v̄` among child modules at each module intersection, weighting the main-axis child against the lateral group by accumulated light; the division is exact, so a child's vigor is never reduced in transit. At module scale, a mature module divides its own direct exposure equally across its terminals, `q(n_i) = Q(u)/#n`, accumulates that over its branch topology, and distributes `v̄(u)` over it to give each terminal a vigor `v` that gates attachment.
- Maximum module vigor bounds Eq. 5 and `D'`, never the propagated flux, so `v̄(u)` may exceed it (ADR 0008).
- The root vigor budget follows accumulated light up to `v̄_rootmax`; see ADR 0009.
- `split_vigor` performs one binary Borchert-Honda split. Main-axis continuations are precomputed from prototype geometry using ADR 0010. Multiple laterals form one group for Eq. 2, then divide their shared budget proportionally by accumulated light using ADR 0011. An all-zero-light fork uses `λ` and `1−λ` directly rather than sending all vigor to main or producing `0/0`.

The earlier Self-organizing Tree Models paper applies the two-pass Borchert-Honda process to buds and branches. Synthetic Silviculture adapts it to branch modules.

### Morphospace selection

A new module's query coordinate is `(λ, D')`:

```text
D' = v̄(u_parent) · D / v̄_max
```

`vigor_scaled_determinacy` calculates `D'`. `nearest_morphospace_prototype` applies the accepted fixed grid (ADR 0005):

```text
 λ high | Cube.006  Cube.007  Cube.008
 λ mid  | Cube.003  Cube.004  Cube.005
 λ low  | Cube      Cube.001  Cube.002
          D low     D mid     D high
```

Each axis uses levels `{1/6, 1/2, 5/6}`. Determinacy increases across columns; apical control increases across rows. Maximum module vigor is the shared constant `v̄_max = 1`, distinct from `v̄_rootmax`. `D' = v̄(u_parent) D / v̄_max` normalizes into `[0, D]` only for a bounded numerator, so the parent's vigor is clamped to `v̄_max` for this query alone (ADR 0005, ADR 0008).

### Orientation

```text
f_distribution(u) = ω₁ · f_collisions(u) + ω₂ · f_tropism(u)    Eq. 3
f_tropism(u_α) = |cos(α_tropism) - cos(u_α)|                    Eq. 4
```

`orientation_distribution_cost` and `orientation_tropism_cost` preserve these equations. Appendix A.1 describes coordinate descent from the parent orientation using two positive/negative horizontal Euler perturbations. A new module instead starts with its root segment aligned to the mature parent terminal tangent, then is rigidly oriented once at attachment using its precomputed mature prototype extent against existing current bounds and previously oriented same-step siblings; later Eq. 10 tropism adaptation bends developing segments without rerunning module orientation (ADR 0013). Search uses Z-up XYZ Euler angles composed as `Rz · Ry · Rx`; interpreting the paper's DirectX coordinates as Y-up maps its horizontal perturbations to positive/negative 10-degree X/Y offsets in plant space. Search runs at most three iterations and stops when no candidate improves cost. The paper provides no numerical absolute-cost error threshold.

### Attachment, shedding, and senescence reference

For each module, every terminal receives `q(n_i) = Q(u) / #n` from the module's own direct exposure, so attachment eligibility does not depend on which sibling terminals are occupied. The private terminal calculation runs in the complete pre-step light/vigor state even for a module that will cross maturity; only mature terminals are exposed publicly. Every mature never-used terminal is reconsidered each step and attaches when its module-scale vigor is strictly above `v̄_min`, while an attached child draws its vigor from the separate plant-scale pass (ADRs 0017 and 0018). Eligible attachments commit together after survivors develop. Their orientations are selected main-axis first, then by decreasing mature-tangent alignment with the main tangent, so progressively more lateral siblings account for earlier selected mature bounds.

After pre-step vigor allocation, every module whose plant-scale `v̄` is strictly below `v̄_min` is marked. Maximal marked roots and their descendant subtrees are removed in one parent-first batch before survivors develop; equality survives, nested marks emit no duplicate event, and the root has no exemption. Surviving IDs remain stable, removed IDs are never reused, and one compact event identifies each removed maximal subtree (ADRs 0014 and 0015). Each terminal may attach at most one module lifetime, so shedding exposes the former parent terminal geometrically but leaves it spent (ADR 0020). Reattachment remains deferred because the paper does not specify it.

Parent terminals and child roots form shared conduit junctions. In one whole-plant postorder, a leaf edge receives support diameter `φ`; every other edge receives `sqrt(sum(child current diameter²))`. Each segment interpolates from `φ` toward that support by its diameter maturity, and retains the greatest diameter it has developed. This produces exact Eq. 8 proportions at mature supported forks, smooth attachment development, and no surviving-pipe shrink after shedding (ADR 0016). The conduit topology remains derived; only one diameter per architecture edge persists.

The paper does not provide a numerical value for `v̄_min`. Growth, attachment, and shedding provisionally share `v̄_min = 0.02` in the module-vigor range `[0, 1]`. A finite-horizon whole-library sweep retained this baseline provisionally: under its tested root, horizons, and timesteps, increases suppressed attachment/development without making additional presets shed. Revisit it with senescence, timestep checks, and explicit acceptance targets (`docs/research/minimum-module-vigor-sweep.md`).

At `p_t ≥ p_max`, the paper linearly reduces root vigor to zero with a constant step but does not provide the decline rate or duration. Senescence behavior remains deferred until healthy growth, attachment, light, and shedding are validated.

## Plant type parameters

The 16 presets retain Synthetic Silviculture Table 4 exactly:

| Paper | Field | Role |
|---|---|---|
| `p_max` | `plant_max_age` | senescence onset |
| `v̄_rootmax` | `root_max_vigor` | maximum whole-plant root vigor budget |
| `g_p` | `plant_growth_rate` | Eq. 5 scale |
| `λ/λ_mature` | `apical_control` / `mature_apical_control` | Eq. 2 and morphospace |
| `D/D_mature` | `determinacy` / `mature_determinacy` | morphospace |
| `F_age` | `flowering_age` | mature-parameter transition |
| `α` | `tropism_angle` | Eq. 4 target |
| `ω₂` | `tropism_weight` | Eq. 3 weight |
| signed `g₁` | `tropism_strength` | Eq. 10 strength/direction |
| `φ` | `terminal_thickness` | Eq. 8 terminal diameter; interpreted as centimeters and converted to meters |
| `β` | `length_growth_scale` | Eq. 9 scale; interpreted as centimeters per physiological-age unit and converted to meters |

`parameter_for_plant_age` selects young or mature values at flowering age. The paper's Eq. 10 `g₁`/`g₂` labels and Table 4's single signed `g₁` column are inconsistent; module growth retains the existing interpretation: the table value controls signed strength while per-segment falloff controls decay.

## Current implementation

`PlantSimulation` owns plant architecture, repeated mature-terminal attachment, batched subtree shedding, atomic stepping, attachment/shedding event summaries, current-state diagnostics, and zero-copy plant snapshots. Its private conduit module derives shared-junction topology and develops Eq. 8 diameters; plant-scale and module-scale light/vigor remain separate. Senescence enters through a later milestone.

## Sources

- Synthetic Silviculture (Makowski et al. 2019): §5.2, §5.3, §5.3.1, Appendix A.1, Table 4.
- Self-organizing tree models for image synthesis (Palubicki et al. 2009): §4.1–4.5, especially extended Borchert-Honda allocation.
