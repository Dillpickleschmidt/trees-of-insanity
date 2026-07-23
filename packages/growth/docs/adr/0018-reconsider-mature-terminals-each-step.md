---
status: accepted
---

# Reconsider mature terminals each step

Evaluate every never-used terminal of every mature module during each plant step, attaching when its module-scale vigor is strictly above `v̄_min`; a module that reaches maturity joins this candidate set in that crossing step. This supersedes 0012's one-shot crossing-only eligibility: Synthetic Silviculture describes attachment for any fully developed module, and repeated evaluation lets terminals recover after earlier low vigor. ADR 0020 separately makes a terminal spent after its first attachment.

Attachment candidates use terminal-use state and light/vigor from the complete state at the start of the step, including for a module that will cross maturity during that step; post-integration state supplies attachment geometry only. Eligible attachments still commit together after surviving modules develop; newborn modules first receive light, vigor, and growth during the following step.
