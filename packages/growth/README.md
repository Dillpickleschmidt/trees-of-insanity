# Trees of Insanity Growth

Standalone C++23 library for deterministic branch-module growth, paper-derived equations, and staged single-plant simulation. `PlantSimulation` currently implements the root-only plant slice; attachment and repeated topology growth follow in later milestones.

```sh
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build
```

Public API: `include/toi/growth/growth.hpp`. The library has no renderer, UI, file-format, persistence, or GPU dependency.
