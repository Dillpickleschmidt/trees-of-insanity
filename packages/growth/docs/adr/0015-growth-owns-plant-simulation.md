---
status: accepted
---

# Growth owns single-plant simulation

`toi::growth` owns deterministic plant architecture behind a stateful deep `PlantSimulation` interface: `create`, `step`, and `snapshot`. It mutates internal storage during atomic steps to avoid large state copies and preserves topology invariants. Desktop and future ecosystem orchestration consume that renderer-independent interface rather than reimplementing light, vigor, development, attachment, orientation, or shedding. Keeping this behavior with its equations maximizes locality and shared correctness without creating another package seam before a second implementation exists. Modules receive monotonically increasing plant-local integer IDs that remain stable for their lifetime and are never reused after shedding, supporting deterministic topology links, snapshots, event summaries, and keyed rendering without global UUIDs. `snapshot()` returns an immutable lightweight view into simulation storage valid until the next step or reset; consumers synchronously build any owning render or UI projection before mutation resumes.
