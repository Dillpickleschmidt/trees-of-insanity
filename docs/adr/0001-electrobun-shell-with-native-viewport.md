---
status: accepted
---

# Use Electrobun shell with a native viewport

## Context

Trees of Insanity needs a Solid/Tailwind interface and a native GPU viewport. The Growth preview must preserve a native ovrtx CUDA output → CUDA/Vulkan interop → Vulkan presentation path, rather than routing frames through webview pixels or a canvas upload path.

The platform order is Linux X11 on NVIDIA RTX desktops first, then Windows native on NVIDIA RTX.

## Decision

Use Electrobun as the desktop shell.

- Solid/Tailwind renders the application interface.
- The right pane is an `<electrobun-wgpu>` native child surface.
- The native child surface handle becomes the Vulkan presentation target.
- The hot path remains native and coarse-grained from the shell.
- The shell uses system webview renderers first: WebKitGTK on Linux and WebView2 on Windows.

## Consequences

- Linux requires WebKitGTK runtime packages.
- CEF is not part of the default shell because the application interface does not need DOM overlays over the native viewport.
- Growth preview controls initially live in the Solid panel rather than as native overlay chrome.
- Windows requires Win32 Vulkan surface and CUDA/Vulkan external-handle support.
- A future language rewrite remains possible, but the first implementation keeps the native hot path in C++.

## Acceptance checks

- Linux opens with the native WebKitGTK renderer on X11.
- The right-pane native child surface reports a valid X11 window handle.
- The Vulkan presenter can create a surface from the native child surface handle.
- No CPU readback or webview canvas upload is used for the main viewport.

## Notes

Automated shell verification has confirmed that the WebKitGTK path reports a native X11 handle. CEF is avoided unless a future requirement needs it.
