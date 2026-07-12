---
status: accepted
---

# Group lateral children for vigor allocation

At a fork with multiple lateral children, apply Synthetic Silviculture Eq. 2 once between the precomputed main child and the lateral group, using the sum of lateral accumulated light. Divide the resulting lateral vigor budget among lateral children proportionally to their accumulated light. This conserves vigor, remains independent of child order, and avoids repeatedly applying apical control to arbitrary pairwise splits. If both main and lateral accumulated light are numerically zero, light provides no preference: split by `λ` and `1−λ`; if every lateral is also zero, divide the lateral group budget equally.
