# DesktopSession owns project and runtime state

`DesktopSession` is the model facade owning one Project plus transient runtime simulations such as `PlantSimulation`. The Project contains project-wide authored content and complete typed Module, Plant, and Ecosystem workspace states, each with independent viewport state. Shell coordinates model snapshots and graphics projections; no renderer or Qt objects enter the model.
