# Growth library architecture

`toi::growth` is one deep module. `include/toi/growth/growth.hpp` is its only supported source API; all algorithms remain private.

Dependency direction:

```text
consumer adapters/renderers -> toi::growth -> C++ standard library
```

The API accepts/returns domain values. It never loads assets or produces renderer state. Prototype source adapters must convert coordinates and source concepts before calling it. Renderers project snapshots afterward.

Paper equations use developer-friendly names with nearby symbol comments. Provisional choices require an ADR and deterministic test.
