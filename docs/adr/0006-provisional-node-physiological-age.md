---
status: accepted
---

# Derive provisional node physiological age from graph path length

Synthetic Silviculture expects node physiological age on branch module prototypes, but the initial bundled prototype source does not encode it. Until authoring data supplies node physiological age directly, prototype preparation derives provisional node physiological age from root-to-node graph path length divided by the plant type length growth scale.

## Consequences

- Provisional node physiological age is an approximation, not a paper-stated rule.
- Code implementing paper equations should keep concise comments mapping paper symbols to developer-friendly names.
- When explicit node physiological age becomes available, the import/preparation adapter can stop deriving it without changing Growth preview or Project concepts.
