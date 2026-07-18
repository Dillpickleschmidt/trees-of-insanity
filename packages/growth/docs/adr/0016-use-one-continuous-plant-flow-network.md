---
status: accepted
---

# Use one continuous conduit network for the pipe model

The conduit network spans module branch topology and terminal attachments as one rooted tree, and serves the Eq. 8 pipe model: diameters develop continuously across module-attachment junctions because a module boundary is not a physical pipe break.

Light and vigor do not use this network. Synthetic Silviculture computes them at two scales that this conduit must not merge: a plant-scale pass over the module tree, and a module-scale terminal pass inside a mature module. See `0020-two-scale-light-and-vigor.md`.
