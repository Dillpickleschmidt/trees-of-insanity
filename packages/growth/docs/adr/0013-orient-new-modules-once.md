---
status: accepted
---

# Orient new modules once at attachment

Start each new module by aligning its root segment with the mature parent terminal's tangent, preserving parent-relative roll through the shortest rotation, then search nearby rigid orientations and apply the selected orientation once. This intentionally replaces Synthetic Silviculture §5.2.3 and Appendix A.1's parent-module starting orientation so growth continues outward from each attachment instead of resetting to the parent's axis. Candidate collision cost uses the new module's mature prototype extent because its age-zero geometry has no meaningful extent; existing modules contribute their current bounds. Existing modules are not rigidly reoriented during later steps—Eq. 10 tropism adaptation bends their branch segments as they develop. Equation 3 uses fixed collision baseline `ω₁ = 1`; only the relative weight matters, and Table 4 supplies `ω₂` as the plant-type tropism weight. Search evaluates the paper's four positive/negative perturbations of the first and third Euler angles, performs at most three coordinate-descent iterations, and stops earlier when no candidate improves cost. The perturbation angle starts as a provisional 10-degree calibration value because the paper specifies only a small angle.
