# Qt desktop monorepo implementation plan

Status: proposed

## Goal

Replace the Electrobun/native-child architecture with one cross-platform composition model:

1. The application owns one Vulkan device compatible with CUDA.
2. Qt Quick adopts that device and owns final window composition/presentation.
3. The native renderer produces app-owned Vulkan images without CPU readback.
4. Qt Quick displays the completed viewport image as a scene-graph texture.
5. One transparent, full-window Qt WebEngine view renders the Solid UI above it.

The final tree is one Git repository containing an independently buildable plant-growth library and one desktop application. No Electrobun, native child windows, DOM masks, alternate modal path, or canvas viewport remains.

## Decisions and assumptions

### Recommended decisions

- Keep one Git repository. Do not use submodules or split repositories.
- Use normal reviewable commits; do not rewrite history. File history is not valuable enough to complicate the migration.
- Keep Bun only as frontend package manager/build tooling. Do not embed Bun or Deno in the shipped application.
- Use Qt WebChannel as the only browser/native bridge.
- Target Linux X11/NVIDIA first and Windows/NVIDIA second with the same application architecture.
- Treat Wayland as a later platform qualification, not a second architecture.
- Do not claim macOS support while the renderer requires CUDA/NVIDIA.
- Support ordinary CSS alpha, rounded clipping, shadows, transitions, and modal backdrops. Do not promise CSS `backdrop-filter` over the Vulkan layer; Chromium cannot blur a separate Qt scene-graph item.
- Pin one exact Qt release after the compositor spike. Qt 6.9 or newer is required; prefer the then-current Qt 6.11 patch release if the spike passes.
- Dynamically link Qt under LGPLv3 while the application is personal software. Re-evaluate licensing before distribution or commercialization.
- Standard CSS translucency is sufficient. True frosted-background blur is not a requirement.
- Support Linux and Windows on NVIDIA/CUDA. macOS is unsupported while the renderer requires CUDA.

## Target repository layout

```text
/
├── AGENTS.md
├── CONTEXT.md                         # repository map; routes agents to local context
├── README.md                          # clone, bootstrap, build, test
├── CMakeLists.txt                     # orchestration only
├── CMakePresets.json
├── package.json                       # Bun workspace + root commands
├── bun.lock
├── cmake/
├── scripts/
├── docs/
│   ├── architecture.md                # repository-level dependency rules
│   ├── implementation-plan.md         # this plan, renamed at cutover
│   ├── agents/domain.md               # multi-context routing instructions
│   └── adr/
│       └── 0001-monorepo-module-boundaries.md
├── packages/
│   └── growth/
│       ├── CMakeLists.txt
│       ├── README.md                  # public consumer documentation
│       ├── CONTEXT.md                 # growth terminology only
│       ├── docs/
│       │   ├── PRD.md
│       │   ├── architecture.md
│       │   ├── model.md
│       │   └── adr/
│       ├── include/toi/growth/
│       │   └── growth.hpp             # intentionally small public seam
│       ├── src/                        # private implementation
│       └── tests/                      # no desktop assets/dependencies
└── desktop/
    ├── CMakeLists.txt
    ├── README.md
    ├── CONTEXT.md                     # workspaces, project, viewport, UI terms
    ├── docs/
    │   ├── PRD.md
    │   ├── architecture.md
    │   └── adr/
    ├── assets/
    ├── src/
    │   ├── model/                     # headless desktop use cases
    │   ├── graphics/                  # projection, ovrtx, CUDA/Vulkan
    │   └── shell/                     # Qt window, scene graph, WebChannel
    ├── ui/
    │   ├── package.json
    │   ├── src/                       # existing Solid application
    │   └── tests/
    └── tests/
        ├── model/
        ├── graphics/
        └── shell/
```

Generated files, Qt, ovrtx, build trees, and packaged artifacts remain outside source directories and ignored.

## Module boundaries

### `toi::growth` — standalone public library

Purpose: deterministic plant-development calculations, independent of the desktop product.

Owns:

- plant type parameters and paper presets;
- branch module prototype domain types;
- prototype preparation and validation;
- module-scale growth;
- plant-scale light, vigor, attachment, shedding, orientation, and senescence;
- module and plant snapshots/summaries;
- domain math required by those operations.

Must not depend on or mention:

- Qt, Bun, Solid, JSON, filesystems, OBJ, USD, ovrtx, CUDA, Vulkan;
- Project persistence, workspaces, viewport preferences, HDRIs, cameras;
- desktop assets or compile-time repository paths.

Public seam:

- domain input/output value types;
- prototype validation/preparation entry point;
- module-development entry point;
- plant-development entry point;
- preset lookup and parameter validation.

Everything else stays private and ordered below public functions by usage. Fold the current `toi::plant` implementation into this package instead of exposing separate `growth` and `plant` libraries. Move `BranchModulePrototypeLibrary` out of the OBJ importer so growth never depends on an adapter. Move `kModulePrototypeImportScale` to the desktop OBJ adapter.

Build acceptance:

```sh
cmake -S packages/growth -B build/growth -G Ninja
cmake --build build/growth
ctest --test-dir build/growth
```

This must work without Qt, ovrtx, CUDA, Vulkan, nlohmann-json, or desktop assets.

### Desktop model — deep private application module

Purpose: headless desktop behavior behind one use-case facade.

Owns:

- Project persistence and fresh schema;
- plant type library ownership;
- OBJ import adapter and bundled prototype loading;
- active workspace/prototype/plant type/ages;
- application and viewport preferences;
- state/query DTOs consumed by the UI;
- orchestration of `toi::growth` requests.

Public seam inside the desktop app:

- create/open a `DesktopSession`;
- query one complete UI state snapshot;
- apply a typed desktop action;
- obtain the current module/plant snapshot for graphics;
- subscribe to state/preview invalidation.

No Qt types, renderer objects, USD, CUDA, Vulkan, or JSON command strings in this module. JSON is allowed only in persistence adapters at its edge.

### Desktop graphics — deep private rendering module

Purpose: turn growth outputs into a GPU image that Qt can sample.

Owns:

- render-chain projection and camera framing;
- USDA/ovrtx stage generation;
- ovrtx renderer session;
- CUDA/Vulkan external-memory images and semaphores;
- double-buffered frame production;
- depth-aware guides;
- orbit camera state;
- native precomposition command buffers and image-layout transitions.

Public seam inside the desktop app:

- initialize using the app-created Vulkan instance/device/physical device/queue;
- set the current preview snapshot and render preferences;
- apply camera input;
- resize to physical pixels;
- expose the current completed display image plus generation/extent/status;
- notify the Qt render thread when a new image is ready.

It does not own a native window, surface, swapchain, or final presentation. It does not know about DOM elements, WebChannel, Project files, or UI controls.

### Qt shell — composition and adapter module

Purpose: own process lifecycle and connect the two deep desktop modules to Qt.

Owns:

- `QGuiApplication`, `QQuickWindow`, and Qt WebEngine initialization;
- Vulkan instance/device creation and Qt device adoption;
- viewport scene-graph item and imported texture wrapper;
- full-window transparent `WebEngineView` above the viewport item;
- WebChannel DTO serialization/action dispatch;
- pointer/wheel routing from Solid to camera actions;
- Qt lifecycle, focus, DPI, resize, hide/show, and device-loss handling;
- packaging metadata and app entry point.

The WebChannel API stays coarse:

- `bootstrap()` returns complete initial state;
- `dispatch(action)` applies a discriminated action;
- native emits `stateChanged`, `previewStatusChanged`, and fatal-error events.

JSON/QVariant is a boundary protocol only. The desktop model receives typed C++ actions.

## Final GPU composition design

### Device ownership

The shell creates the Vulkan instance, selects the CUDA-matching NVIDIA physical device, creates the Vulkan device/queues, and enables the union of:

- Qt's preferred instance/device extensions for an imported device;
- swapchain and Qt Quick requirements;
- CUDA external-memory and external-semaphore extensions;
- timeline semaphore support;
- current renderer-required formats/features.

Before scene-graph initialization:

- force Qt Quick's graphics API to Vulkan;
- associate the `QVulkanInstance` with the window;
- call `QQuickWindow::setGraphicsDevice(QQuickGraphicsDevice::fromDeviceObjects(...))`;
- ensure Qt and graphics use the same queue family and device lifetime.

Qt never destroys app-owned Vulkan objects. The shell destroys Qt scene-graph resources before destroying the device.

### Frame synchronization

Do not depend on `QRhi::setQueueSubmitParams()` for CUDA-ready waits. Its Vulkan API does not expose timeline values and fixes wait stages too late for fragment texture reads.

Use the existing app-owned timeline synchronization in a small native precomposition submission executed on the Qt render thread and on Qt's adopted queue:

1. Wait for CUDA's timeline value for the newly completed slot.
2. Transition the CUDA color/distance images from `GENERAL` as needed.
3. Blit color and draw depth-aware guides into an app-owned display image.
4. Return CUDA images to `GENERAL`.
5. Transition the display image to shader-read layout.
6. Signal slot reuse/completion synchronization.
7. Communicate the final native layout to QRhi before Qt records sampling.
8. Qt submits its scene graph and presents afterward on the same queue.

All `vkQueueSubmit*` calls on the shared queue occur on the Qt render thread. Worker threads may run ovrtx/CUDA, but may not submit to that queue. Vulkan device-level resource creation/destruction is serialized and scheduled at safe scene-graph stages.

### Scene-graph viewport

Prefer a standard `QQuickItem`/scene-graph texture node over `QQuickRhiItemRenderer`, which remains preliminary.

- Wrap each app-owned display `VkImage` with a non-owning `QRhiTexture::createFrom()`.
- Wrap that with `QQuickWindow::createTextureFromRhiTexture()`.
- Keep native image and QRhi layout metadata synchronized explicitly.
- The viewport item is below WebEngine in QML stacking order.
- The item receives its logical rectangle from the Solid layout over WebChannel; the graphics module receives physical pixel extent using Qt's device-pixel ratio.

### Web UI composition

- WebEngine fills the window and has `backgroundColor: "transparent"`.
- HTML/root backgrounds are transparent over the viewport region.
- Panels and chrome paint opaque backgrounds.
- Floating controls paint normal DOM alpha, rounded corners, shadows, and transitions.
- DOM receives all pointer input. Camera interaction remains browser-side pointer tracking with frame-coalesced native actions.
- Buttons/popovers consume events before camera routing.

No masks, holes, native UI replicas, hidden viewport modal behavior, or platform-specific composition fallback remains.

## Documentation replacement

Delete the current root `CONTEXT.md` content and all current root ADRs after their still-valid decisions are rewritten in their owning context. Do not retain superseded ADRs merely for history.

### Root documentation

- `CONTEXT.md`: repository map and ownership only.
- `docs/architecture.md`: dependency direction and public-seam rules.
- Root ADR 0001: one repository, independently buildable growth package, desktop app dependency direction.
- `docs/agents/domain.md`: replace single-context instructions with nearest-context routing.

### Growth documentation

- `CONTEXT.md`: only plant-growth glossary and paper terminology.
- `docs/PRD.md`: deterministic reusable library requirements and exclusions.
- `docs/architecture.md`: public contract, purity, dependency rules, numerical/testing policy.
- `docs/model.md`: replace/move `single-plant-growth-model.md`.
- Rewrite/consolidate growth ADRs:
  1. Z-up coordinates.
  2. Import-neutral branch module prototypes.
  3. Provisional node physiological age.
  4. Growth outputs remain renderer-independent.
  5. Single-plant self-collision light.
  6. Morphospace prototype grid.
  7. Provisional plant-scale integration choices.

### Desktop documentation

- `CONTEXT.md`: product/workspace/Project/viewport/UI glossary.
- `docs/PRD.md`: desktop workflows, supported platforms, performance and accessibility requirements.
- `docs/architecture.md`: model/graphics/shell/UI modules and runtime data flow.
- New desktop ADRs:
  1. Qt Quick and Qt WebEngine own one composited window.
  2. App-created Vulkan device is adopted by Qt.
  3. Native precomposition synchronizes CUDA frames before Qt sampling.
  4. Solid UI communicates through one WebChannel adapter.
  5. Desktop session owns Project/application state.
  6. Local Project uses an unreleased fresh schema.
  7. OBJ is a desktop import adapter.
  8. ovrtx consumes render-chain projections, not growth domain state.
  9. Depth-aware guides are graphics-owned overlays.

Delete the completed milestone TODO rather than preserving it as active documentation. Move any still-useful acceptance criteria into the PRDs/tests.

## Explicit legacy deletion inventory

Delete, not deprecate or wrap:

- `electrobun.config.ts`;
- Electrobun dependency and all Electrobun scripts/configuration;
- `src/bun/**`;
- `native/**` and the C FFI shared-library seam;
- `src/mainview/electrobun.d.ts`;
- `src/mainview/shell.ts`;
- `src/shared/shellRpc.ts`;
- generic C++ JSON application-command dispatch after WebChannel cutover;
- `tests/nativeCore.test.ts`;
- Electrobun shell automation and `scripts/verify-shell.ts`;
- `<electrobun-wgpu>` and its `Viewport.tsx` host;
- X11 native-surface discovery;
- app-owned Vulkan swapchain/presenter classes;
- modal/offscreen-transform viewport behavior;
- DOM mask/punch-through experiments if any are introduced during the spike;
- root ADRs replaced by package/app ADRs;
- stale build/dist artifacts and references to old source paths.

Move and refactor rather than delete:

- `growth` + `plant` → `packages/growth`;
- `import`, `project`, and headless application behavior → desktop model internals;
- `render`, `ovrtx`, CUDA interop, viewport overlay/session logic → desktop graphics internals;
- Solid components/styles → `desktop/ui`;
- prototype/HDRI assets → `desktop/assets`;
- relevant tests → owning package/module.

Final source must contain no `electrobun`, `WGPUView`, `<electrobun-wgpu>`, native viewport handle, `toi_native_core`, or old shell RPC references.

## Implementation sequence

### Phase 0 — record decisions and establish gates

1. Commit this plan.
2. Select the exact Qt release for the spike; do not add fallback Qt versions.
3. Define performance baselines from the current application: viewport render throughput, camera latency, resize behavior, idle GPU, startup time, package size.

Exit: decisions recorded; baseline artifact stored.

### Phase 1 — extract the standalone growth package

1. Create `packages/growth` with standalone CMake and tests.
2. Move current growth and plant algorithms.
3. Fold `toi::plant` into `toi::growth` internals/public result types.
4. Move prototype-library domain ownership out of OBJ import.
5. Remove filesystem/import-scale/desktop compile definitions from growth.
6. Replace tests that load desktop OBJ assets with package-local programmatic fixtures.
7. Establish install/export target `toi::growth` and public include contract.
8. Write growth context, PRD, architecture, model reference, and ADRs.

Exit:

- package builds/tests standalone;
- no forbidden dependency/include/path;
- all 16 presets remain deterministic and bounded;
- desktop still builds against the moved package during transition.

### Phase 2 — restructure the headless desktop model

1. Create desktop model target and `DesktopSession` facade.
2. Move Project persistence, OBJ import, preferences, and use-case state behind it.
3. Replace renderer-returning controller methods with growth snapshot requests/invalidation.
4. Remove the current public proliferation of `toi_import`, `toi_project`, and `toi_app` targets; make implementation headers private.
5. Replace generic string command logic with typed model actions.
6. Rehome headless tests under desktop model.

Exit: model tests build without Qt, ovrtx, CUDA, or Vulkan.

### Phase 3 — prove Qt composition before destructive shell cutover

Build a temporary compositor spike, then delete its experimental scaffolding after production integration.

Required proof:

1. Qt adopts an app-created Vulkan device.
2. App-created Vulkan image appears through a scene-graph texture node.
3. Transparent WebEngine renders a moving rounded 50%-alpha Solid button and modal over the image.
4. Native precomposition submission waits a timeline semaphore and transitions layouts without validation errors.
5. Qt hide/show, resize, shutdown, and scene-graph invalidation preserve correct ownership.
6. Linux X11/NVIDIA and Windows/NVIDIA use the same source architecture.
7. `chrome://gpu` confirms accelerated WebEngine composition.
8. No CPU viewport readback occurs.

Hard stop: if Linux or Windows requires a second visual/input architecture, do not proceed with Qt migration; reassess framework choice.

### Phase 4 — implement the production Qt shell

1. Add Qt entry point/window/QML composition.
2. Create/adopt Vulkan device before scene-graph initialization.
3. Implement WebEngine resource loading, transparent background, devtools, and WebChannel.
4. Implement viewport texture item and DPR-aware geometry updates.
5. Implement typed Qt bridge over `DesktopSession`.
6. Add fatal startup diagnostics for missing CUDA/Vulkan/Qt GPU requirements; do not silently use software rendering.
7. Add Qt lifecycle and teardown ordering.

Exit: shell runs against a synthetic Vulkan frame and complete Solid UI.

### Phase 5 — refactor graphics into a Qt texture producer

1. Move render projection and ovrtx modules into desktop graphics.
2. Refactor Vulkan context to use injected instance/device/queue instead of creating a native surface/device.
3. Delete swapchain/presenter/native-surface ownership.
4. Keep CUDA color/distance slots and renderer overlap.
5. Add app-owned display images for precomposition.
6. Record native precomposition waits, blits, guide draws, layout transitions, and reuse signals.
7. Expose completed display images/generation/status to the Qt texture item.
8. Preserve event-driven rendering: no new growth frame while idle; cached image can be recomposited for UI-only changes.
9. Handle resize by rebuilding size-dependent graphics resources at physical pixel size while Qt may display the previous complete image.

Exit: real ovrtx plant preview appears under transparent DOM; camera and guide controls work.

### Phase 6 — move frontend and cut over bridge

1. Move frontend into `desktop/ui` and update Vite aliases/build output.
2. Replace Electrobun RPC client with one typed WebChannel client.
3. Keep loading semantics (`undefined` not loaded, `[]` loaded empty).
4. Route camera input through DOM and prevent control bubbling.
5. Remove viewport hiding from modal state.
6. Add actual floating rounded/translucent controls as the composition acceptance fixture.
7. Build UI into Qt resources or a release-safe custom URL scheme; no localhost server in release.

Exit: all current workflows operate through Qt/WebChannel.

### Phase 7 — atomic legacy removal

1. Delete every path in the legacy inventory.
2. Remove Electrobun/Bun-runtime dependencies and FFI build targets.
3. Remove old CMake targets/includes/compile definitions.
4. Remove obsolete tests/scripts/docs.
5. Run repository-wide forbidden-reference search.
6. Ensure clean checkout builds without stale generated files masking missing dependencies.

Exit: only Qt architecture exists.

### Phase 8 — documentation and packaging cutover

1. Install the final root/growth/desktop documentation layout.
2. Delete/replace old root ADRs and completed plans.
3. Rewrite README/bootstrap commands.
4. Pin Qt and ovrtx acquisition; keep SDKs under ignored `.cache` paths.
5. Add Windows deployment through `windeployqt` and Linux packaging with all WebEngine helpers/resources/sandbox files.
6. Include Qt/Chromium licenses and LGPL replacement instructions where required.
7. Update automation to launch packaged Qt app and inspect native status events/screenshots.

Exit: one clone plus documented bootstrap builds/tests/packages each supported target.

## Verification matrix

### Growth package

- standalone configure/build/test;
- no nonstandard runtime dependency;
- deterministic module/plant outputs;
- all presets bounded;
- validation/error behavior;
- paper equation tests and provisional-choice tests.

### Desktop model

- Project create/load/save/validation;
- OBJ adapter conventions;
- workspace and plant type actions;
- fresh schema only, no migrations;
- preview invalidation behavior;
- no Qt/GPU dependency.

### Desktop graphics

- Vulkan validation layers clean;
- CUDA/Vulkan device UUID match;
- timeline/precomposition ordering under long renders;
- no slot overwritten while Qt samples it;
- exact color/depth generation pairing;
- resize storms, hide/show, device loss, shutdown;
- idle renderer remains event-driven;
- no CPU viewport readback.

### Shell/UI

- real alpha over moving viewport, including rounded corners and shadows;
- modal backdrop leaves viewport visible and dimmed;
- button/popover input never triggers camera;
- keyboard, focus trap, IME, accessibility tree;
- DPR and mixed-DPI monitor movement;
- Chromium GPU acceleration mandatory;
- dev/release resource loading;
- packaged Linux and Windows launches.

### Performance gates

At the same resolution/scene as baseline:

- no more than 5% reduction in viewport render throughput;
- no new device-wide synchronization in the steady-state hot path;
- no CPU frame copy/readback;
- camera input remains frame-coalesced;
- UI-only animation may recompose the cached viewport but may not rerender growth;
- settled UI and viewport do not continuously generate work;
- record startup memory/package-size increase as an accepted Qt WebEngine cost.

## Commit strategy

Use one migration branch and reviewable commits, approximately:

1. `Document monorepo and Qt architecture plan`
2. `Extract standalone growth package`
3. `Consolidate desktop model behind session seam`
4. `Prove Qt Vulkan and transparent WebEngine composition`
5. `Add production Qt shell and WebChannel`
6. `Adapt viewport renderer to Qt texture composition`
7. `Move Solid UI and cut over bridge`
8. `Remove Electrobun and native-child architecture`
9. `Replace documentation and add packaging`

Keep each commit buildable where practical. Temporary dual architecture is acceptable only inside migration history; the final tree has one architecture. Do not create separate Git repositories or preserve dead compatibility code.

## Definition of done

- One clone contains the independently buildable growth package and desktop app.
- Growth has a small renderer/file/UI-neutral public API and private deep implementation.
- Desktop model and graphics expose narrow seams; implementation headers are private.
- Qt owns the only window compositor and final presentation path.
- Solid DOM renders genuine rounded/translucent UI above the viewport.
- CUDA → Vulkan remains GPU-only and asynchronous.
- Linux and Windows use the same app-level architecture.
- Electrobun, FFI shell, native child surface, old swapchain presenter, mask/hide workarounds, old commands, and obsolete docs are absent.
- Documentation is independently scoped for repository, growth library, and desktop product.
- Growth, model, graphics, UI, shell, package, and performance verification all pass.
