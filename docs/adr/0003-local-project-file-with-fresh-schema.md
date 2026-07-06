---
status: accepted
---

# Use a local project file with a fresh schema

Trees of Insanity persists a Project as a local file containing the selected branch module prototype, the plant type library, and the active plant type. The Project schema is allowed to change while the application is unreleased; implementation should avoid compatibility layers for unused older schemas.

## Consequences

- Project load/save validation is preserved as a core application behavior.
- Project schema changes should be made directly and tested through the native application-command module.
- No migration or fallback code is needed until there are released Project files to support.
