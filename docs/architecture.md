# Repository architecture

## Layout

- `packages/growth`: installable CMake library; no desktop dependencies.
- `desktop/src/model`: Project persistence, OBJ import, and `DesktopSession`.
- `desktop/src/graphics`: render projections, ovrtx, CUDA/Vulkan interop.
- `desktop/src/shell`: Qt Quick/WebEngine composition and WebChannel adapter.
- `desktop/ui`: Solid frontend; Bun is build tooling only.

## Dependency direction

`growth <- desktop model <- shell`

`growth <- render projection <- ovrtx/viewport <- shell`

`desktop UI -> WebChannel adapter -> DesktopSession`

No lower layer includes Qt. Growth excludes filesystem, serialization, import, renderer, CUDA, and Vulkan concerns. Public APIs stay narrow; implementation details remain private to their target.

## Builds

- Growth standalone: `cmake -S packages/growth -B build/growth`
- Headless model/tests: `cmake --preset core`
- Desktop: `cmake --preset desktop`
- UI: `bun run build:ui`
