---
status: accepted
---

# Use hybrid rendering for Plant diagnostics

Render Plant-workspace spheres, flow paths, and terminal markers through the native depth-aware GPU overlay, while rendering module diagnostic labels as Solid DOM elements for normal typography, formatting, and theming. The GPU overlay is a deep graphics module: its small interface accepts generic batched world lines, branch-surface paths, and markers, with no Plant-workspace concepts. Plant render projection alone translates plant diagnostics into those primitives, so other workspaces carry no plant-specific options or logic.

Branch-surface paths store centerline, tangent, and host radius; the overlay places them on the camera-facing host surface before applying ordinary scene-distance occlusion. Their own branch cannot hide them, while unrelated foreground geometry can. A shader-global time value animates dash phase without CPU geometry updates: accumulated light moves toward the root in cyan and vigor moves toward terminals in amber. Path width represents its fraction of the corresponding root total within a fixed pixel range, keeping allocation readable as plant size grows while labels retain absolute values. Native graphics also projects module-root label anchors into viewport coordinates and sends screen positions and values through a narrow bridge; TypeScript does not duplicate camera math. Labels use a fixed screen-space offset, hide only outside the view or behind the camera, and deliberately perform no overlap avoidance. Terminal markers appear only on mature modules: occupied terminals are filled, unoccupied terminals are hollow, and color encodes terminal vigor. This avoids custom GPU font and label-layout systems without compromising depth-aware geometric diagnostics.
