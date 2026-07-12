---
status: accepted
---

# Orient new modules once at attachment

Follow Synthetic Silviculture §5.2.3 and Appendix A.1 by starting each new module at its parent module's orientation, searching nearby rigid orientations, and applying the selected orientation once. Candidate collision cost uses the new module's mature prototype extent because its age-zero geometry has no meaningful extent; existing modules contribute their current bounds. Existing modules are not rigidly reoriented during later steps—Eq. 10 tropism adaptation bends their branch segments as they develop. Equation 3 uses fixed collision baseline `ω₁ = 1`; only the relative weight matters, and Table 4 supplies `ω₂` as the plant-type tropism weight. Search evaluates the paper's four positive/negative perturbations of the first and third Euler angles, performs at most three coordinate-descent iterations, and stops earlier when no candidate improves cost. The perturbation angle starts as a provisional 10-degree calibration value because the paper specifies only a small angle.
