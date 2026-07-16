# Desktop architecture

## Model

`DesktopSession` owns one Project and transient runtime simulations. The Project contains project-wide authored content plus complete typed Module, Plant, and Ecosystem workspace states, each with independent viewport state and no fallback inheritance. Import and persistence are private adapters. Model has no Qt, CUDA, Vulkan, ovrtx, or render-projection dependency. `toi::growth::PlantSimulation` owns root development, first-generation attachment, conduit pipes, and requested flow diagnostics; Ecosystem remains a placeholder.

## Graphics

Render projection converts growth snapshots to renderer inputs. ovrtx writes color and requested scene distance into CUDA memory. `PreviewRenderer` double-buffers exportable Vulkan images, coordinates CUDA/Vulkan with a timeline semaphore, and precomposes depth-aware diagnostics on Qt's render thread. Plant light/vigor uses the exact pipe triangles as an animated Vulkan surface material; animation reuses cached ovrtx color/distance and changes only command recording plus a time push constant. Qt samples the completed image as a scene-graph texture. No CPU pixel transfer occurs.

Live resize aspect-fits the last complete frame instead of stretching it. After viewport geometry settles, the shell requests a device-pixel render extent; the render worker pauses while Qt's render thread replaces only the CUDA-interoperable frame slots. A stable maximum-size Vulkan display texture avoids scene-graph texture churn and unsafe image retirement.

## Shell

Qt creates the CUDA-compatible Vulkan device and adopts it for the Quick scene graph. QML places `ViewportTextureItem` below a transparent WebEngine view. `DesktopBridge` exposes actions, viewport geometry, camera input, and status through WebChannel. Shell alone coordinates model and graphics, including Plant step/reset/timestep/diagnostic actions.

## UI

Solid owns controls and browser pointer interpretation. The frontend sends coarse actions, reports viewport bounds, and coalesces camera input. It has no native handles or GPU API knowledge. Plant is enabled incrementally through the accepted milestones; Ecosystem remains visible and disabled.

## Runtime flow

UI workspace action → WebChannel → `DesktopSession` → growth snapshot → render projection → ovrtx CUDA frame → timeline semaphore → Vulkan precomposition → Qt scene graph → one window.
