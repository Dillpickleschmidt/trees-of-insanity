---
status: accepted
---

# Plant steps are atomic

A plant step calculates light, vigor, development, and topology decisions from one complete plant state, then commits the next complete state atomically. The Plant workspace inspects complete snapshots and compact attachment/shedding event summaries rather than retaining partially updated phase states; this keeps execution simpler and prevents phase-order interactions from leaking into persistent state.
