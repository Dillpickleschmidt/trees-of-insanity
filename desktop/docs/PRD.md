# Desktop PRD

## Product

A local desktop editor for branch-module and plant development. Users select prototypes and plant types, scrub physiological age, edit plant parameters, save one Project, inspect structure, and orbit/pan/dolly an RTX preview.

## Requirements

- Linux and Windows on NVIDIA/CUDA; one architecture on both.
- One Qt-composited window with Solid controls over a native Vulkan texture.
- GPU-only asynchronous CUDA→Vulkan frame path; no CPU image readback.
- Interactive camera changes coalesce and never block the UI thread.
- Loaded/empty UI states remain distinct and controls are keyboard accessible.
- Ordinary rounded, shadowed, translucent CSS; cross-layer backdrop blur not required.
- Bun builds frontend assets but is not shipped.
- Dynamically link Qt under LGPLv3 obligations; review licensing before commercialization.

## Quality gates

Core and growth tests pass independently. UI typecheck/build passes. Desktop starts, edits refresh the preview, camera controls work, and model/graphics errors reach viewport status. No second native window or viewport exists.
