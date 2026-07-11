# Desktop context

The desktop product edits one local Project through module and plant workspaces.

- `DesktopSession`: authoritative Project, workspace, growth age, and viewport preferences.
- Model: persistence and import adapters; no Qt or GPU code.
- Graphics: projects growth snapshots, renders with ovrtx, and transfers frames CUDA→Vulkan.
- Shell: Qt-owned window/device/composition and WebChannel boundary.
- UI: transparent Solid controls rendered by Qt WebEngine over the native viewport texture.

Supported: Linux and Windows with NVIDIA/CUDA. macOS unsupported. Use the same Qt/Vulkan/CUDA architecture on both supported platforms.
