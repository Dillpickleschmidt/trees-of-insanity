# ADR 0001: Monorepo product boundaries

Status: accepted

Keep growth and desktop in one Git repository. `packages/growth` remains independently configurable, buildable, testable, and installable. Desktop depends inward on growth; growth cannot depend on desktop. Repository-level tooling may orchestrate both without merging their public APIs or documentation contexts.
