# Trees of Insanity Growth

Standalone C++23 library for deterministic branch-module growth, paper-derived equations, and staged single-plant simulation. The current reset point retains module growth and equation primitives; whole-plant behavior is added through the accepted incremental milestones.

```sh
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build
```

Public API: `include/toi/growth/growth.hpp`. The library has no renderer, UI, file-format, persistence, or GPU dependency.
