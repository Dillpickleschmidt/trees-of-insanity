---
status: accepted
---

# Use each terminal once

A mature terminal may attach at most one module during its lifetime. Shedding removes the child subtree but leaves the former parent terminal spent; every mature never-used terminal is still reconsidered each step under ADR 0018. Synthetic Silviculture does not specify post-shedding reuse, so this supersedes ADR 0019's recovery latch with the smaller and more paper-conservative baseline while reattachment remains deferred for separate study.
