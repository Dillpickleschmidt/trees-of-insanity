---
status: accepted
---

# Preserve the Plant camera across simulation steps

Initialize the Plant-workspace camera from mature root-module bounds, then preserve its orbit target, orientation, and distance as plant steps change geometry and topology. Growth never automatically reframes the camera; an explicit future Frame plant action may fit current complete bounds. Resetting the simulation resets the camera. This keeps visual scale stable for human comparison and avoids apparent shrinking or jumps during growth.
