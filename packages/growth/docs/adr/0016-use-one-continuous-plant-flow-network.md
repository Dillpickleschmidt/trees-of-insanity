---
status: superseded by ADR-0019
---

# Use one continuous plant flow network

Represent basipetal light and acropetal vigor as one continuous network through module branch topology and terminal attachments. An immature module is a leaf whose accumulated light equals its direct light exposure. A mature module divides its direct exposure equally among terminals; an occupied terminal adds its child's accumulated light, then branch topology carries the total toward the module root. Vigor traverses the same topology in reverse: occupied terminals feed existing children, while eligible unoccupied terminals attach children. This composes the paper's module-scale and plant-scale passes without duplicate traversal rules and directly supplies continuous diagnostic paths.
