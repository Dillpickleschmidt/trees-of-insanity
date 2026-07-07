---
status: accepted
---

# Double-buffer the growth preview between CUDA and Vulkan

The growth preview is built for continuous rendering — growth playback, camera drags, and eventually full ecosystem scenes — not just a static frame. The viewport therefore pipelines the two GPU engines instead of serializing them: CUDA renders and copies frame N into one shared slot while Vulkan blits and samples frame N−1 from the other.

Each slot holds a shared color image (ovrtx `LdrColor`), a shared scene-distance image (`DistanceToCameraSD`, when guides are on), and the camera that produced them, so the guide overlay always matches the frame actually on screen. Renderer outputs are mapped as CUDA arrays and their render-completion events are waited on the copy stream, so the copies are ordered after the finished render without any device-wide synchronization. A CUDA event tells the CPU when a slot's copies land (to swap the slots), and a shared timeline semaphore carries the same ordering to Vulkan: the present submit waits for the slot's timeline value before reading it. Timeline waits are non-consuming, so re-presenting a cached frame re-waits the same value safely.

The render loop stays event-driven on top of the pipeline: input is polled every tick, a new frame is produced only when something changed (stage, camera, guide toggle, resize), and a settled preview skips presenting entirely, so an idle viewport costs ~0% GPU. While a frame is cooking, the previous frame keeps presenting, so interaction feedback stays at display rate even when renders are slow.

## Consequences

- Continuous growth playback and camera drags run at the renderer's full rate; copies, blits, and presentation overlap the next render instead of extending each frame.
- Frame throughput scales with scene cost alone — there is no per-frame device-wide stall to amortize, which matters as scenes grow toward whole ecosystems.
- The viewport shows a complete frame or the previous one, never a partial render; guide occlusion samples the distance image of the exact frame displayed.
- Vulkan 1.2 timeline semaphores are required (enabled at device creation) alongside the existing external-memory/semaphore interop extensions.
- Two copies of the color and distance images exist instead of one; at preview resolutions this is a few tens of megabytes of GPU memory.
