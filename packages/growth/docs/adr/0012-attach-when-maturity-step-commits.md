---
status: accepted
---

# Attach children when the maturity-crossing step commits

A module is mature when its integrated physiological age reaches or exceeds its fully grown age. Every unoccupied terminal whose vigor exceeds `v̄_min` attaches a child in the topology commit of that same atomic plant step at physiological age `0`. Eligibility, vigor, and prototype selection use the same pre-commit state. Children are oriented against committed modules and previously oriented siblings, then applied as one topology batch. The order is precomputed per prepared prototype: main-axis terminal first, then laterals by decreasing mature-tangent alignment with the main tangent, with node ID breaking ties. Terminal occupancy is derived from current attachments, so shedding a child makes its former terminal available again without historical occupancy state. New children first receive light, vigor, and growth during the following step. Smaller simulation timesteps improve event timing when needed without introducing recursive mid-step births.
