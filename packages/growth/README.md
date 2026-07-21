# Trees of Insanity Growth

Standalone C++23 library for deterministic branch-module growth, paper-derived equations, and staged single-plant simulation. `PlantSimulation` implements arbitrary-depth repeated attachment, whole-plant conduit pipes, and current-state diagnostics; shedding follows in the next milestone.

```sh
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build
```

Public API: `include/toi/growth/growth.hpp`. The library has no renderer, UI, file-format, persistence, or GPU dependency.
