---
status: accepted
---

# Reconsider mature terminals each step

Evaluate every unoccupied terminal of every mature module during each plant step, attaching when its module-scale vigor is strictly above `v̄_min`; a module that reaches maturity joins this candidate set in that crossing step. This supersedes 0012's one-shot crossing-only eligibility: Synthetic Silviculture describes attachment for any fully developed module, and repeated evaluation lets never-occupied terminals recover after earlier low vigor. Terminals vacated by shedding additionally follow the recovery gate in ADR 0019.

Attachment candidates use occupancy and light/vigor from the complete state at the start of the step, including for a module that will cross maturity during that step; post-integration state supplies attachment geometry only. A terminal whose child subtree is shed therefore becomes unoccupied in the committed state and first advances its recovery state during the following step, preserving atomic decisions without making attachment depend on an earlier topology mutation within the same commit. Eligible attachments still commit together after surviving modules develop; newborn modules first receive light, vigor, and growth during the following step.
