# Desktop product requirements

## Product

A local desktop plant-modeling application with typed Module, Plant, and Ecosystem workspaces. Module previews one branch-module prototype at a directly selected physiological age. Plant runs a transient deterministic `toi::growth::PlantSimulation` with human-verifiable diagnostics. Ecosystem remains a disabled placeholder until its simulation exists.

One fresh Project stores project-wide authored content plus complete typed state for every workspace, including independent viewport state. No schema migration or fallback inheritance is required before release.

## Requirements

- Linux and Windows on NVIDIA/CUDA; one architecture on both.
- One Qt-composited window with Solid controls over a native Vulkan texture.
- GPU-only asynchronous CUDA→Vulkan frame path; no CPU image readback.
- Interactive camera changes coalesce and never block the UI thread.
- Module and Plant keep independent controls, selections, cameras, environments, and viewport diagnostics.
- Plant runs advance through atomic steps as quickly as possible to an absolute target age, support stopping between steps, and coalesce previews to the configurable Viewport FPS limit.
- Plant diagnostics provide independent labels, direct-light spheres, accumulated-light flow, vigor flow, and mature-terminal toggles.
- Ecosystem cannot activate until implemented.
- Loaded/empty UI states remain distinct and controls are keyboard accessible.
- Ordinary rounded, shadowed, translucent CSS; cross-layer backdrop blur not required.
- Bun builds frontend assets but is not shipped.
- Dynamically link Qt under LGPLv3 obligations; review licensing before commercialization.

## Quality gates

Core and growth tests pass independently. UI typecheck/build passes. Desktop starts, Module edits and Plant steps refresh the preview, camera controls work, and model/graphics errors reach viewport status. No second native window or viewport exists.
