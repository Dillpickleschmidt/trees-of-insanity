# Growth library architecture

`toi::growth` is one deep module. `include/toi/growth/growth.hpp` is its only supported source interface. It exposes module development, paper equations, and deterministic plant creation/stepping while hiding traversal and topology orchestration.

Dependency direction:

```text
consumer adapters/renderers -> toi::growth -> C++ standard library
```

The API accepts/returns domain values. It never loads assets or produces renderer state. Prototype source adapters must convert coordinates and source concepts before calling it. Renderers project snapshots afterward.

Paper equations use developer-friendly names with nearby symbol comments. Plant steps return renderer-independent state and diagnostics. Provisional choices require documentation and deterministic validation.

## Collision performance staging

Initial single-plant development uses deterministic all-pairs module-sphere evaluation as the correctness oracle. Before Ecosystem implementation, replace only this private implementation with a spatial broad phase such as a grid or BVH, retaining stable accumulation order and exact fixture equivalence. The `PlantSimulation` interface, snapshots, and consumers do not change.
