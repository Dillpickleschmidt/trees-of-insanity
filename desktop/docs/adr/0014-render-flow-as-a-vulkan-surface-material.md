---
status: accepted
---

# Render flow as a Vulkan surface material

Replace camera-facing flow ribbons with animated circumferential bands drawn over the exact pipe surface mesh in the Vulkan diagnostic pass. Reusing pipe triangles and longitudinal distance avoids floating geometry, orbit-dependent orientation, and separately tessellated rings; scene-distance comparison preserves foreground occlusion. Do not implement flow as an ovrtx/USD material: diagnostics remain optional, light and vigor remain independently controllable, and animation updates only a Vulkan time uniform over the cached ovrtx frame rather than forcing a ray-traced scene render each frame. This supersedes only the flow-path representation described by ADR-0012.
