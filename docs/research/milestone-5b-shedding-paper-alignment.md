# Milestone 5B shedding: paper-alignment research

## Conclusion

Synthetic Silviculture specifies one shedding rule exactly:

```text
shed module u when plant-scale vigor v̄(u) < v̄_min
```

It does not supply `v̄_min`, a full detach algorithm, terminal-reuse timing, event semantics, or an anti-oscillation mechanism. Milestone 5B must preserve those distinctions: paper facts below, previously accepted project policy where the paper is silent, and explicit approval for remaining choices.

## Primary evidence

| Question | Evidence | Consequence |
|---|---|---|
| Step dependency | “At each simulation step we first estimate the light exposure `Q` for each module and calculate its growth potential vigor `v̄`. Then, we determine how quickly each module develops … and whether, where, and how to attach or detach modules.” [Synthetic Silviculture, §5.2, article p. 131:4](https://wp.faculty.wmi.amu.edu.pl/papers/Makowski.etal-2019-Synthetic-Silviculture.pdf) | Shedding consumes the current complete plant’s light/vigor result. The paper does not order detachment relative to age integration or attachment more precisely. |
| Shedding quantity and predicate | “We define a shedding threshold (`v̄_min`) that defines when a module is shed (`v̄ < v̄_min`).” [§5.2.1, p. 131:5](https://wp.faculty.wmi.amu.edu.pl/papers/Makowski.etal-2019-Synthetic-Silviculture.pdf) | Use plant-scale module vigor `v̄`, never module-internal terminal vigor `v`. Comparison is strict; equality survives. |
| Removal unit | “At plant scale, we add and remove branch modules…” [§5 opening, p. 131:4](https://wp.faculty.wmi.amu.edu.pl/papers/Makowski.etal-2019-Synthetic-Silviculture.pdf) | Remove module instances, not individual segments. |
| Root | Senescence reduces root vigor “until all modules are shed from the architecture.” [§5.2.1, p. 131:5](https://wp.faculty.wmi.amu.edu.pl/papers/Makowski.etal-2019-Synthetic-Silviculture.pdf) | No root exemption is described. Supporting an empty/dead architecture is the closest reading and is already accepted by ADR 0014. |
| Descendants | Modules form an ordered rooted tree, the “module architecture.” [§5.2, p. 131:4](https://wp.faculty.wmi.amu.edu.pl/papers/Makowski.etal-2019-Synthetic-Silviculture.pdf) | The paper does not say “subtree,” but retaining descendants after their parent is removed would violate its connected rooted architecture. Subtree removal is an implementation invariant and accepted milestone policy. |
| Mature attachment | “For any module `u` where `a_u > a_mature` … At each terminal node `n` with vigor `v > v̄_min`, we attach a new branch module.” [§5.3, article pp. 131:6–131:7](https://wp.faculty.wmi.amu.edu.pl/papers/Makowski.etal-2019-Synthetic-Silviculture.pdf) | The paper describes evaluating fully developed modules, not one-shot maturity-crossing eligibility. General mature-terminal retry is closer to the text than the superseded crossing-only policy. The paper’s `>` conflicts with its statement that age stops at the maximum; the project’s existing “reaches maturity” interpretation remains necessary. |
| Terminal reuse | Terminal nodes serve as connectors for other modules during growth. [§5.1, p. 131:4](https://wp.faculty.wmi.amu.edu.pl/papers/Makowski.etal-2019-Synthetic-Silviculture.pdf) | Reuse after child removal is not discussed. Evaluating every mature unoccupied terminal naturally permits it, but same-step versus next-step reuse is unresolved. |
| Threshold value | `v̄_min` appears in the shedding predicate and Eq. 5 but not Table 4 or another numerical table. [§§5.2.1, 5.3; Table 4](https://wp.faculty.wmi.amu.edu.pl/papers/Makowski.etal-2019-Synthetic-Silviculture.pdf) | `0.02` is approved provisional project calibration, not a paper value. |
| Collision after removal | Eq. 1 sums intersections with modules in the current architecture. [§5.2.1, Eq. 1, p. 131:5](https://wp.faculty.wmi.amu.edu.pl/papers/Makowski.etal-2019-Synthetic-Silviculture.pdf) | Removed modules must not contribute to later committed collision/light evaluation. Same-step recomputation is unspecified. |
| Pipe after removal | Synthetic Silviculture gives Eq. 8 but no post-shedding diameter rule. Its source model states branch width “is not decreased when leaves and branches are shed or pruned” and therefore requires memory of past structure. [Pałubicki et al. 2009, §4.5](https://algorithmicbotany.org/papers/selforg.sig2009.small.pdf) | ADR 0016’s retained surviving diameters are consistent with the inherited primary model; do not shrink surviving pipes. |
| Senescence | At `p_t ≥ p_max`, root vigor declines by a constant step until all modules shed, but no decline rate/duration is supplied. [Synthetic Silviculture, §5.2.1](https://wp.faculty.wmi.amu.edu.pl/papers/Makowski.etal-2019-Synthetic-Silviculture.pdf) | Keep decline policy/calibration in Milestone 6. 5B should only make threshold shedding and empty architecture possible. |

## Explicit paper gaps

The paper does **not** define:

- numerical `v̄_min`;
- detach-before-development versus detach-after-development;
- detach-before-attachment versus attach-before-detach;
- whether a terminal vacated during a step may reattach in that same step;
- simultaneous mark handling, ancestor deduplication, or within-step cascades;
- stable IDs, shed events, event ordering, or event payloads;
- hysteresis, cooldown, minimum lifetime, or another attach/shed oscillation guard;
- post-shed collision recomputation timing;
- post-shed pipe shrinkage in Synthetic Silviculture itself.

No first-party source implementation was found. The [Graphics Replicability Stamp Initiative record](https://replicability.graphics/papers/10.1145-3306346.3323039/index.html) reports no author code. ACM lists a small [first-party supplement ZIP](https://dl.acm.org/doi/suppl/10.1145/3306346.3323039/suppl_file/a131-makowski.zip); available metadata and the paper identify supplementary glossary material, not an algorithm or parameter release. Third-party implementations were excluded as authority.

## Already accepted project policy

These choices are not paper claims, but already have explicit project decisions:

- ADR 0014: mark strict-below-threshold modules after vigor allocation; shed descendant subtrees before growth; collapse overlapping marks to highest ancestors; root may shed.
- ADR 0015: `PlantSimulation` owns atomic topology mutation; IDs are stable, monotonic, and never reused.
- ADR 0016: transient conduit topology; surviving developed diameters never shrink.
- ADR 0017: shedding uses plant-scale `v̄`; terminal attachment uses distinct module-scale `v`.
- Milestone 5: provisional shared `v̄_min = 0.02`; deterministic shedding with single-use terminals.
- Milestone 6: root-vigor senescence decline remains deferred.

## Approved project decisions

1. **Mature attachment:** evaluate every mature never-used terminal each step; a newly mature module is first evaluated in its crossing step. ADR 0018 supersedes crossing-only ADR 0012.
2. **Eligibility state:** terminal-use state and light/vigor come from the complete pre-step state, including for modules that cross maturity; post-integration evaluation supplies attachment geometry only.
3. **Terminal lifetime:** each terminal may attach at most one module. Shedding leaves the former parent terminal spent; reattachment is deferred for separate study (ADR 0020).
4. **Threshold calibration:** retain provisional `v̄_min = 0.02`; the finite-horizon sweep in `minimum-module-vigor-sweep.md` found that, under its tested root and timesteps, increases suppressed attachment/development without making additional presets shed.
5. **Shed events:** emit one compact event per maximal removed subtree with its root module ID and former optional parent module ID/terminal node.
6. **Dead runs:** an empty/dead simulation continues advancing to its requested target age rather than stopping implicitly.
7. **Oscillation history:** stateless reuse and a same-threshold recovery latch were both evaluated; ADR 0020 supersedes the recovery latch with the smaller single-use baseline because the paper does not specify reuse.
