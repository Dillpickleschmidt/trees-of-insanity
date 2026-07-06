# Native Core

Integration target: C++ growth/render/project core behind a coarse C ABI, with ovrtx/Vulkan attaching later.

Boundary shape:

- UI sends commands/settings.
- Native core owns simulation, render projection, ovrtx session, and render loop.
- `toi_handle_command` receives JSON application commands.
- Viewport attachment will pass a native surface handle and pixel size.
- Render projection uses bulk buffers, not per-plant/module calls.

Rust can replace or own parts of the core later if the ovrtx/Vulkan boundary stays coarse-grained.
