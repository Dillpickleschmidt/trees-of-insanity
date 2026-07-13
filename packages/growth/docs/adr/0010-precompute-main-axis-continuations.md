---
status: accepted
---

# Precompute main-axis continuations

Prototype preparation classifies the main child at every fork once, so runtime light and vigor passes use direct lookups. Following those continuations to the module tip also precomputes its single main-axis terminal; every other terminal is lateral. Children within 10 degrees of the straightest continuation are treated as equivalently aligned; selection then prefers larger pipe-diameter factor, exact alignment, longer downstream path, and finally stable segment order. Diameter factor captures subtree support while downstream path captures reach, so they are retained as distinct criteria. This avoids tiny angular differences overriding clearly stronger continuations without adding repeated runtime calculations.
