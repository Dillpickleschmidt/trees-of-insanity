---
status: accepted
---

# Render module diagnostics as a Vulkan surface tint

Draw module vigor and module accumulated light as a flat per-module tint over the exact pipe surface mesh in the Vulkan diagnostic pass. Reusing pipe triangles avoids floating geometry, orbit-dependent orientation, and separately tessellated rings; scene-distance comparison preserves foreground occlusion.

Both quantities are per-module scalars, so a module's whole surface takes one colour and nothing animates along its length. Vigor maps over `[0, v̄_max]`, and a module above that maximum renders magenta, outside the colormap, marking where further allocation no longer raises the growth rate. Accumulated light has no fixed ceiling, so the root normalizes it. Mature modules are dimmed: they have stopped developing, so their vigor no longer drives growth, and dimming keeps still-developing modules legible against a crown that is saturated by construction.

Do not implement this as an ovrtx/USD material: diagnostics remain optional and toggling one must not force a ray-traced scene render.
