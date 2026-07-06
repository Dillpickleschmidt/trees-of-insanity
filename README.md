# Trees of Insanity

Clean Electrobun rebuild of Trees of Insanity.

## Direction

- Electrobun desktop shell
- Solid/Tailwind left UI
- native right viewport through `<electrobun-wgpu>` native handle
- system webview renderer first: WebKitGTK on Linux, WebView2 on Windows
- current C++ growth/ovrtx/Vulkan hot path first
- Wayland workstation with Xwayland-backed viewport first
- Windows native + NVIDIA RTX second

Out of scope for now: true Wayland-native viewport, macOS, AMD/Intel GPU support, canvas/WebGPU pixel upload viewport.

## Dev

```bash
bun install
bun run dev
```

Use HMR:

```bash
bun run dev:watch
```

The first spike goal is logging a valid native viewport handle from the right-pane native child surface, then attaching the existing Vulkan presenter to that handle.

## Native core

```bash
cmake --preset core
cmake --build --preset core
ctest --preset core
```

## Automation

```bash
bun run verify:shell
```

This launches the shell, writes JSONL events to `artifacts/automation/`, captures logs, and exits non-zero if the native viewport does not report a handle.
