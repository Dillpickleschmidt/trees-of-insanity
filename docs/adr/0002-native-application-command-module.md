---
status: accepted
---

# Keep application commands owned by the native core

Trees of Insanity uses a native application-command module as the canonical seam for project loading, plant type library changes, branch module prototype selection, module physiological age changes, prototype inspection, and Growth preview projection requests. Electrobun, Solid, automation, and tests call this seam through adapters; they do not own duplicate project, growth, or render-projection state.

## Consequences

- The shell may keep transient UI state, but persisted project state and growth state live behind the native application-command module.
- JSON/RPC is an adapter protocol, not the domain model.
- Tests should exercise application behavior through the same command seam used by the shell.
- The native command interface should stay coarse-grained enough to preserve the hot path and avoid chatty per-branch-module calls.
