---
status: accepted
---

# Group lateral children for vigor allocation

At a fork with multiple lateral children, apply Synthetic Silviculture Eq. 2 once between the precomputed main child and the lateral group, using the sum of lateral accumulated light. Divide the resulting lateral vigor budget among lateral children proportionally to their accumulated light. This conserves vigor, remains independent of child order, and avoids repeatedly applying apical control to arbitrary pairwise splits. If both main and lateral accumulated light are numerically zero, light provides no preference: split by `λ` and `1−λ`; if every lateral is also zero, divide the lateral group budget equally.

Proportional-by-light division is the equal-weight case of the N-way allocation in Self-organizing Tree Models, `v_i = v · Q_i w_i / Σ Q_j w_j`. Synthetic Silviculture replaced that paper's per-branch priority weights with the single apical-control parameter and supplies no `w_i`, so equal weights are the only division its parameters support. The same rule covers a module intersection whose main-axis terminal carries no child: Eq. 2 then gives the main branch zero light, and the entire flux is the lateral group's.
