# Minimum module vigor sweep

## Question

Is provisional `v̄_min = 0.02` too low, especially given that Plant Type O shows almost no visible shedding by age 2000?

Synthetic Silviculture defines the shared threshold but supplies no numerical value. This experiment therefore evaluates project calibration, not paper correctness.

## Method

Primary inputs were the committed `PlantSimulation`, all 16 Table 4 presets, and the bundled nine-prototype OBJ library.

For each threshold `{0.005, 0.01, 0.02, 0.03, 0.04, 0.05, 0.08}`:

- compile the same simulation with only `kMinimumModuleVigor` changed;
- create each preset `a`–`p` from root prototype `Cube.008`;
- use single-use terminals from ADR 0020;
- run 300 ten-year steps to plant age 3000;
- count final/maximum modules, attachments, removed modules, maximal-subtree events, first shedding age, and root death;
- use deterministic all-pairs collision evaluation.

A second sweep reproduced the current workspace’s Type O run through age 2000 with one-year steps for thresholds `0.01`–`0.05`.

No senescence was active. A temporary `/tmp` harness performed the experiment; no calibration seam or benchmark code was added to production.

This is a sensitivity sweep, not a converged calibration: it fixes one root prototype, uses finite horizons, does not compare multiple timesteps for every preset, and has no botanical or visual acceptance targets. Event timing and forward-Euler development are timestep-sensitive. The one-year Type O run checks the user-observed scenario but does not establish convergence for the whole library.

## Whole-library result

| `v̄_min` | Final modules, all presets | Attachments | Removed modules | Presets shedding | Root deaths |
|---:|---:|---:|---:|---:|---:|
| 0.005 | 11,992 | 19,901 | 7,925 | 4/16 | 0 |
| 0.010 | 10,679 | 16,251 | 5,588 | 4/16 | 0 |
| **0.020** | **8,068** | **13,506** | **5,454** | **5/16** | **0** |
| 0.030 | 6,899 | 12,313 | 5,430 | 5/16 | 0 |
| 0.040 | 6,052 | 11,225 | 5,189 | 5/16 | 0 |
| 0.050 | 5,379 | 9,975 | 4,612 | 5/16 | 0 |
| 0.080 | 3,248 | 5,620 | 2,388 | 5/16 | 0 |

Raising the threshold did not make shedding universal: the same small set of types accounted for nearly all turnover. It mainly prevented physiological development and terminal attachment, reducing architecture size before shedding could occur.

At `0.02`, shedding began at:

| Preset | First shedding age | Final modules | Removed modules |
|---|---:|---:|---:|
| b | 2160 | 330 | 37 |
| f | 1350 | 2,978 | 957 |
| l | 2700 | 1,198 | 1 |
| n | 360 | 996 | 4,003 |
| o | 1890 | 2,094 | 456 |

The other 11 presets did not shed by age 3000. Increasing the threshold to `0.05` still produced shedding in only these five presets, while reducing several non-shedding architectures to a root plus one generation:

| Preset | Final at 0.02 | Final at 0.05 |
|---|---:|---:|
| e | 42 | 18 |
| g | 51 | 18 |
| h | 45 | 18 |
| i | 93 | 48 |
| m | 66 | 18 |
| p | 30 | 18 |

## Type O at the current workspace settings

Age 2000, one-year steps:

| `v̄_min` | Final modules | Attachments | Removed modules | First shedding age |
|---:|---:|---:|---:|---:|
| 0.01 | 882 | 888 | 7 | 1591 |
| **0.02** | **519** | **519** | **1** | **1909** |
| 0.03 | 452 | 463 | 12 | 1872 |
| 0.04 | 343 | 347 | 5 | 1592 |
| 0.05 | 589 | 636 | 48 | 1325 |

The response is nonlinear. A higher threshold does not monotonically reduce final size or increase shedding because it simultaneously changes Eq. 5 development, terminal attachment, competition, and later shedding.

At `0.02`, Type O’s only removal by age 2000 happened in a step that also attached 14 modules, changing the module count from 431 to 444. The disappearance is therefore real but visually masked by simultaneous growth.

## Interpretation

Within these tested scenarios, the sweep does not show that `0.02` is too low as a shared baseline:

- substantial turnover already occurs in presets f, n, and o;
- tested increases suppressed many architectures before making additional presets shed;
- no tested threshold made more than five presets shed by age 3000;
- no roots died in the sweep;
- Type O’s near-invisible age-2000 shedding reflects that specific horizon/timestep and simultaneous attachment, not a general calibration verdict.

The threshold is structurally high-leverage because the paper uses the same symbol for growth, attachment, and shedding. Changing it to make one workspace visibly prune would change all three mechanisms across the library.

## Recommendation

Keep `v̄_min = 0.02` provisionally for the currently tested scenarios.

Do not raise it merely to make Type O visibly shed by age 2000. For visible validation, run Type O beyond age 2000 or inspect presets f/n; a future shedding-event diagnostic would make small removals observable without altering biology.

Revisit the value only alongside senescence and explicit whole-plant acceptance targets. If independent control of growth, attachment, and shedding is desired, that is a new model decision—not calibration of the paper’s shared `v̄_min`.
