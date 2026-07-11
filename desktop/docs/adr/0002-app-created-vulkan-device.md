# ADR 0002: Shell-created Vulkan device

Status: accepted

The shell selects a Vulkan physical device matching the CUDA UUID, enables required external-memory/semaphore extensions, creates the device/queue, and makes Qt adopt those objects. Qt owns presentation; graphics borrows the device view.
