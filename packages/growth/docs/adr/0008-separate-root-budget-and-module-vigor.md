---
status: accepted
---

# Separate the root vigor budget from module vigor

`v̄_rootmax` is the Table 4 cap on a whole plant's root vigor budget, while `v̄_max` is the shared maximum vigor of one module and equals `1`. Synthetic Silviculture uses distinct notation, explicitly clamps each module to `v̄_max`, and does not list it as a plant-type parameter; a numerical prototype also showed that equating both maxima prevents nearly every preset's lone root from growing. Root prototype selection therefore queries at full module vigor, `D′ = 1 · D / 1 = D`.
