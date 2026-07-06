---
status: accepted
---

# Treat OBJ as a branch module prototype import source

Branch module prototypes are domain objects, not OBJ objects, vertices, or line records. OBJ files may populate the branch module prototype library through an import adapter, but after import the application works with branch nodes, branch segments, node physiological age, maximum segment length, and other branch module prototype data.

## Consequences

- OBJ parsing stays behind an adapter seam.
- Growth, Project, Prototype inspector, and render projection modules do not expose OBJ concepts.
- Bundled OBJ imports use deterministic conventions: object names sorted for internal prototype IDs, the first imported node as root, coincident vertices collapsed, root-relative positions, and source coordinates converted into application coordinates.
