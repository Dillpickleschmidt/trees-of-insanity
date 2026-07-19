---
status: accepted
---

# Decouple simulation and rendering cadence

A Plant run advances as quickly as possible through atomic steps until its absolute target age. The configured step size controls accuracy; the final step may be smaller so the run lands exactly on its target. Stop takes effect between steps and preserves the latest completed state.

Simulation and rendering have independent cadences. Intermediate states are coalesced to the application-level Viewport FPS limit, and completion or stop always presents the final state. The same limit caps native scene rendering and diagnostic animation without changing numerical results. Per-step camera recentering remains visible during the run, but its Project update is persisted only when the run finishes so simulation's hot loop performs no Project I/O.
