# Desktop architecture

## Model

`DesktopSession` owns Project state and returns growth-owned snapshots. Import and persistence are private adapters. Model has no Qt, CUDA, Vulkan, ovrtx, or render-projection dependency.

## Graphics

Render projection converts snapshots to renderer inputs. ovrtx writes color into CUDA memory. `PreviewRenderer` double-buffers exportable Vulkan images, coordinates CUDA/Vulkan with a timeline semaphore, and precomposes layout/transitions on Qt's render thread. Qt samples the completed image as a scene-graph texture. No CPU pixel transfer occurs.

## Shell

Qt creates the CUDA-compatible Vulkan device and adopts it for the Quick scene graph. QML places `ViewportTextureItem` below a transparent WebEngine view. `DesktopBridge` exposes bootstrap/actions, viewport geometry, camera input, and status through WebChannel. Shell alone coordinates model and graphics.

## UI

Solid owns controls and browser pointer interpretation. The frontend sends coarse actions, reports viewport bounds, and coalesces camera input. It has no native handles or GPU API knowledge.

## Runtime flow

UI action → WebChannel → `DesktopSession` → snapshot → render projection → ovrtx CUDA frame → timeline semaphore → Vulkan precomposition → Qt scene graph → one window.
