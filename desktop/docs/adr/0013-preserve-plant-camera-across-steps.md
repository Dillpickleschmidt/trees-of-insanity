---
status: accepted
---

# Recenter the Plant camera vertically across simulation steps

Initialize the Plant-workspace camera from mature root-module bounds. After every successful Plant step, set only the orbit target's vertical coordinate to the midpoint of the current rendered segment bounds. Preserve the horizontal target, distance, azimuth, and elevation. Resetting the simulation fully reframes the camera.

This keeps the growing plant vertically centered without changing visual scale, rotation, or horizontal pan.
