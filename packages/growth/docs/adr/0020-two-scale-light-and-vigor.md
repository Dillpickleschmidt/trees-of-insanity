---
status: accepted
---

# Compute light and vigor at the paper's two scales

Synthetic Silviculture separates plant-scale vigor `v̄(u)` from module-scale terminal vigor `v`, and the implementation keeps them separate.

**Plant scale.** A basipetal pass over the module tree accumulates light, `Q_accumulated(u) = Q_direct(u) + Σ Q_accumulated(child)`. An acropetal pass then divides vigor at each module intersection by Eq. 2, weighting the main-axis child against the lateral group by their accumulated light. The division is exact, so a module's vigor reaches its children undiminished. The result drives Eq. 5 growth and morphospace `D′`.

**Module scale.** For a mature module, terminal light is its own direct exposure divided equally, `q(n_i) = Q(u)/#n`, accumulated over the module's branch topology; the module's vigor is then distributed over that light to give each terminal a vigor `v`. An unoccupied terminal with `v > v̄_min` attaches a child. `Q(u)` here is direct exposure, matching Eq. 1, so terminal eligibility does not depend on which sibling terminals are already occupied — routing child-subtree light into this pass lets an established subtree starve its free siblings out of ever attaching.

The parent's branch topology therefore shapes where children attach, not how much vigor each child receives. A newly attached child holds no vigor during the step that creates it, because the paper orders a step as light, vigor, development, then attachment.

Where the paper leaves a case open: with no child on the main-axis terminal, Eq. 2 gives the main branch zero light and the whole flux belongs to the lateral group, which `0011-group-lateral-vigor-allocation.md` divides. The module-scale pass distributes the module's own `v̄(u)`, the only vigor quantity available to it, since the paper specifies that pass's light input but not its base.
