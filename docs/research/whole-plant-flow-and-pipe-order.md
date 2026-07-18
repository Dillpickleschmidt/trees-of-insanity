# Whole-plant conduit, flow, and visualization order

## Recommendation

Implement **conduit/pipe → on-demand flow diagnostics → continuous pipe mesh → Vulkan surface material → overlay-only animation**.

The growth-owned conduit seam must be stable before graphics consumes it. Shader work first would preserve current module boundaries, duplicate topology, and be discarded.

## Primary-source constraints

- Every plant step estimates module light exposure `Q`, performs basipetal light accumulation and acropetal vigor distribution, and advances module physiological age from scalar module vigor. *Synthetic Silviculture* §§5.2.1–5.3, article pp. 131:5–131:7.
- Eq. 8 is the pipe-model area rule: parent diameter is the square root of summed child squared diameters; a leaf uses terminal thickness `φ`. The paper identifies this as Pipe Model Theory with exponent two. *Synthetic Silviculture* §5.3, Eq. 8, article p. 131:7.
- New modules begin with one very small root segment so attachment does not introduce a diameter discontinuity. *Synthetic Silviculture* §5.3, article p. 131:7.
- A module is a developmental unit whose segment length, diameter, and tropism change with physiological age. A module boundary is not a physical pipe break. *Synthetic Silviculture* §§5.1, 5.3, article pp. 131:4, 131:7.
- For mature modules, direct light is divided equally among terminal nodes before terminal vigor is calculated. The paper does not define where direct light enters an incomplete internal module tree. *Synthetic Silviculture* §5.3, article p. 131:7.
- The paper does not define diagnostic animation, color, normalization, surface patterns, immature-module flow visualization, or post-shedding radial shrinkage.

## Accepted project policy

- **Plant conduit network:** continuous rooted plant structure shared by pipe, light, and vigor calculations.
- Parent terminal and child root are one shared junction, not a zero-length edge.
- Modules/prototypes/attachments remain canonical. A contiguous conduit workset is derived per update; no second persistent graph or public global graph IDs.
- Edge identity remains `(module_id, source_segment_id)`.
- Pipe support diameter is `φ` at a leaf or `sqrt(sum(child current diameter²))` otherwise. Current diameter interpolates from `φ` toward support by segment maturity. Surviving edges retain their maximum developed diameter, preventing growth and shedding jumps.
- Module light/vigor remain simulation truth. Edge-level flow through immature modules is growth-owned diagnostic information derived only when requested.
- An immature module's direct light splits equally across its developed frontier: leaf points of its non-zero-length local segment graph. This becomes the paper's equal-terminal rule at maturity.
- Diagnostics describe the current committed state, not the values from the preceding transition.
- Flow amount remains normalized by the corresponding plant-root total for display.
- Flow renders as a Vulkan material-like pass over the exact pipe surface mesh. It is not an ovrtx material and does not create ribbons or separately tessellated rings.
- Animation must recompose cached ovrtx color/depth; it must not rerender the ray-traced scene.

See growth ADRs 0016 and 0018, and desktop ADR 0014.

## Current mismatch

- `PlantSimulation::rebuild_snapshot` combines collision light, module traversal, module-local node fields, dynamic pipe factors, snapshot construction, and flow extraction.
- `dynamic_pipe_factors` and `child_module_by_node` cross attachments through explicit module-boundary special cases.
- Immature light exists only at a module root while immature vigor traverses prototype topology; this asymmetry is accidental.
- Flow paths are computed before integration but retained over post-integration geometry, producing one-step lag.
- Flow data is built independently of UI toggles; render projection merely filters it afterward.
- Plant render chains restart per module and add a child-root start cap.
- Vulkan expands each path into a camera-facing ribbon instead of shading the pipe surface.
- Animated paths mark the whole preview dirty, causing another ovrtx render each animation frame.

## Implementation stages

### 1. Whole-plant conduit and pipe

Create one private growth-owned conduit workset from module records, shared prototype topology, current geometry, and attachments. Alias attached local nodes to one junction. Fill deterministic contiguous edge/node arrays plus preorder/postorder; use transient indices internally.

Run one postorder diameter pass. For each edge, calculate support from updated child current diameters, interpolate by segment maturity, then retain the edge's historical maximum. Emit flat segment snapshots and cross-module continuation metadata from this result. Remove target-diameter, root-supply, and module-boundary pipe helpers.

**Gate:** attachment does not change diameter; diameters are monotonic; mature forks satisfy Eq. 8; shedding cannot shrink surviving pipes; ordering remains deterministic; all work is `O(V+E)`.

### 2. On-demand current-state diagnostics

Expose diagnostic detail through a small growth interface with light/vigor request bits, not workspace concepts. If neither flow is requested, allocate and compute no edge-flow output. Vigor requests may internally require light scratch.

For each module, derive active local edges from non-zero developed length and find active local leaves. Split direct `Q` equally over those leaves, accumulate basipetally through shared junctions, then route vigor acropetally with the existing main/lateral Borchert-Honda policy. Materialize raw requested edge values, root totals, and route distance only for the current committed state.

**Gate:** hand-computable axis/fork/parent-child fixtures conserve totals; immature flow reaches current tips only; enabling diagnostics without stepping matches visible geometry; disabled diagnostics expose no flow allocation.

### 3. Continuous pipe mesh

Build render chains from whole-plant continuation metadata rather than module ranges. Continue through child-root attachments, omit child-root caps, preserve uncapped lateral junction starts, and retain only true root/exposed-tip caps. Encode cumulative world distance and source-edge identity on exact surface faces.

**Gate:** no module seam, cap, UV reset, or phase reset; ordinary pipe rendering remains unchanged elsewhere.

### 4. Vulkan pipe-surface material

Replace `DiagnosticOverlayPath` with a generic surface-field primitive. Reuse exact pipe triangles when diagnostics are enabled. The fragment shader draws circumferential bands from longitudinal world distance and a time uniform, colors them with the existing root-normalized weight map, and animates light rootward/vigor frontierward. Compare interpolated surface distance with ovrtx scene distance so the host surface passes while foreground geometry occludes.

**Gate:** fixed-camera and orbit captures show no float, flip, seam, z-fighting, or undeveloped flow. Independent and simultaneous light/vigor toggles work. Buffers grow explicitly or report resource exhaustion; no silent truncation.

### 5. Overlay-only animation

Separate scene dirtiness from overlay animation dirtiness. Keep clean ovrtx color/depth for the displayed frame and rerun only Vulkan precomposition on animation ticks. Stop ticking with no animated diagnostic.

**Gate:** instrumentation shows ovrtx render count remains constant while band phase advances; representative CPU upload and GPU frame times stay bounded.

## Performance

- Conduit construction and each pass: `O(V+E)` contiguous work.
- Persistent non-diagnostic pipe cost: one diameter per surviving edge.
- Flow scratch/output: only when explicitly requested; Ecosystem plants do not retain it.
- Vulkan surface buffers: only for the diagnostic Plant preview and reused across animation frames.
- Collision exposure remains the existing deterministic `O(M²)` oracle until the ecosystem broad-phase milestone; do not mix that work into this refactor.

## Deferred visual tuning

Band spacing, speed, axial thickness, and simultaneous-channel blending are reversible presentation choices. Tune them from GPU captures after the structural and diagnostic gates pass.
