---
status: accepted
---

# Draw Growth preview guides as a depth-aware viewport overlay

Growth preview guides (such as the world-origin axes) are viewport aids, not branch module instance geometry. They are generated from the Growth preview camera and drawn by the native viewport after the renderer produces its outputs, rather than being added to the USDA/render chain meshes.

The renderer produces an `LdrColor` output and a `DistanceToCameraSD` output for the same frame. The viewport presents the color, copies the distance CUDA array into a shared Vulkan `R32_SFLOAT` image, and draws the guide lines in a Vulkan pass that samples that distance to occlude fragments behind the plant and ground geometry. The color image and the distance image are both written by CUDA and shared with Vulkan through external memory; a shared semaphore orders the CUDA copies before the Vulkan blit and the overlay's distance sampling.

## Consequences

- Guides stay out of the growth model and renderer geometry, leaving one viewport-owned seam for future grids, labels, and gizmo handles.
- Guides are occluded by scene geometry, so they read as part of the 3D scene instead of floating over it.
- The distance output is only requested on frames that actually draw guides, so a guides-off frame renders color alone.
- The depth test is biased by a fraction of the camera focus distance, so guides lying on a surface are not erased by z-fighting while still being hidden behind clearly nearer geometry.
