# Desktop context

The desktop product owns one local Project with project-wide content and typed state for Module, Plant, and Ecosystem workspaces.

## Language

- **Module workspace**: previews deterministic development of one branch module prototype at a directly selected module physiological age.
- **Plant workspace**: simulates one plant architecture from a root module using plant-scale light, vigor, development, and topology decisions. A root-only architecture is still a plant simulation, not a module preview.
- **Plant step**: one atomic transition from one complete plant state to the next.
- **Simulation timestep**: years advanced by each plant step; it remains fixed while simulation runs and can be changed only while paused without resetting the plant.
- **Plant reset**: recreate the transient age-zero plant when plant-type or prototype-library growth configuration changes; camera, timestep, and diagnostic/view changes preserve the current plant.
- **Module diagnostic label**: Plant-workspace world-space readout anchored at every module root showing `Direct light exposure`, `Accumulated light`, and `Vigor`. It does not require hover or selection and does not display an internal module identifier.
- **Flow overlay**: Plant-workspace GPU-animated dashed paths showing light toward the root in cyan and vigor toward terminals in amber. It shows only flow that occurred in a completed plant step, never predicted or inactive routes.
- **Plant diagnostic controls**: independent toggles for module diagnostic labels, direct-light bounding spheres, accumulated-light flow, vigor flow, and mature-terminal markers. Mature terminals render filled when occupied and hollow when unoccupied, with color encoding terminal vigor.
- **Diagnostic rendering**: a graphics-owned overlay module accepts generic batched world lines, branch-surface paths, and markers; it contains no Plant-workspace semantics. Plant render projection supplies those primitives and native-projected label anchors, while module diagnostic labels use Solid DOM elements.
- **Plant camera**: persistent orbit camera initialized from mature root-module bounds, preserved across plant steps, and reframed to current plant bounds only by explicit user action.

- **Project-wide state**: shared authored content and active-workspace selection.
- **Workspace state**: complete typed state owned by one workspace, including its own viewport state; values never fall back through project-wide defaults.
- **Application preferences**: device/user presentation state such as UI theme, panel width, and window geometry; never Project content.

- `DesktopSession`: model facade owning the Project and transient runtime simulations.
- Model: persistence and import adapters; no Qt or GPU code.
- Graphics: projects module growth snapshots and transfers ovrtx frames CUDA→Vulkan.
- Shell: Qt-owned window/device/composition and WebChannel boundary.
- UI: transparent Solid controls over the native viewport texture.
- Workspace tabs: Module and the root-only Plant slice are implemented; Ecosystem remains a disabled placeholder until its own simulation exists.

Plant types, presets, Table 4 parameters, prototype assets/import, and Project CRUD remain because module growth consumes them.

Supported: Linux and Windows with NVIDIA/CUDA. macOS unsupported. Use the same Qt/Vulkan/CUDA architecture on both supported platforms.
