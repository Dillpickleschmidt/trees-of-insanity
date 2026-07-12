# Trees of Insanity

Monorepo containing an independently buildable C++ growth library and a Qt desktop plant-modeling application.

## Products

- [`packages/growth`](packages/growth): deterministic module growth, paper equations, and staged single-plant simulation.
- [`desktop`](desktop): Module workspace plus the staged Plant workspace, using Qt Quick/WebEngine, Solid UI, and an ovrtx CUDA→Vulkan viewport. Ecosystem remains a disabled placeholder.

Supported desktop targets: Linux and Windows on NVIDIA/CUDA. macOS unsupported.

## Build growth only

```bash
cmake -S packages/growth -B build/growth -G Ninja
cmake --build build/growth
ctest --test-dir build/growth --output-on-failure
```

## Build headless desktop model/tests

```bash
cmake --preset core
cmake --build --preset core
ctest --preset core
```

## Build desktop

Requires CUDA, Vulkan, ovrtx, and Qt 6.11.1 components Core, Gui, Qml, Quick, WebChannel, and WebEngineQuick. The current preset resolves ovrtx from `.cache/ovrtx/current` and supplemental Qt packages from `.cache/qt-root`.

```bash
bun install --frozen-lockfile
bun run build
build/desktop/trees_of_insanity
```

Qt is dynamically linked under LGPLv3 obligations. See `CONTEXT.md` and nearest product context before development.
