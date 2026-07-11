# ADR 0003: Native precomposition synchronization

Status: accepted

ovrtx output remains GPU-resident. CUDA signals an imported Vulkan timeline semaphore after copying into a double-buffered image. On Qt's render thread, graphics waits, blits/precomposes, transitions the image for sampling, then exposes it to the scene graph. No CPU readback or Qt queue-submit hook is used.
