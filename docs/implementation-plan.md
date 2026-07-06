# Trees of Insanity implementation plan

## Findings

- Electrobun Linux native renderer currently forces `GDK_BACKEND=x11`; `<electrobun-wgpu>` returns an X11 `Window` XID.
- First target: Wayland workstation with Xwayland-backed native viewport.
- Current shell spike still has `any` in Electrobun/RPC typing; clean that first.
- Old app has useful seams: `growth`, `import`, `render`, `project`, `app`.
- Old wx shell is removable.
- Vulkan attach needs `Display*`; Electrobun gives only XID, so native core can `XOpenDisplay(nullptr)` and validate with `XGetWindowAttributes`.

## Porting stance

- Do not mechanically copy old files just to preserve structure.
- Treat old repo as behavior/algorithm reference, not target architecture.
- Rewrite fresh modules when that removes multi-iteration residue, obsolete concepts, or accidental coupling.
- Preserve product behavior and domain concepts; drop old implementation clutter.
- Prefer simple public seams over compatibility layers.
- No legacy/fallback behavior unless it protects a current target.

## External review / tests

- Use Claude Code / Opus 4.8 opportunistically during implementation for:
  - architecture double-checks,
  - refactor review,
  - test selection and test authoring.
- Prefer Claude for tests so tests stay minimal and behavior-focused.
- Avoid tests that only prove removed/refactored code is gone.
- Add tests only for behavior worth protecting or bugs likely to regress.

## Preservation ledger

### Keep

- Solid/Tailwind UI.
- Native GPU viewport, not canvas/WebGPU pixel upload as main path.
- C++ hot path for growth/render/ovrtx/Vulkan.
- Application-command seam as canonical app state boundary.
- Project persistence semantics, with fresh schema allowed.
- Branch module prototype import from OBJ.
- Z-up application coordinates.
- Render projection separate from growth model.
- Render chain meshes for Growth preview.
- Module physiological age slider and growth summary.
- Prototype inspector.
- Plant type library/presets/project save.
- Orbit/dolly camera.
- Depth-aware guides/world-origin axes.
- HDRI environment/backdrop preferences.
- Screenshot-coordinate automation seam.

### Rework / cleanup

- wx shell -> Electrobun native renderer + Solid UI.
- wx viewport panel -> native core attached to Electrobun WGPU XID.
- wx viewport controls/popovers -> left Solid panel controls.
- frontend native bridge -> typed Electrobun RPC + C ABI command seam.
- old CMake/frontend coupling -> Bun/Vite UI build + native core build.
- old agent-control server -> Bun Playwright-like adapter.
- `wx_vulkan_surface` -> `x11_vulkan_surface`.
- old copied UI components -> reshape to current layout and remove unused concepts.

### Drop intentionally

- wxWidgets desktop shell.
- CEF first path.
- DOM-over-native-viewport requirement.
- main viewport via CPU readback/canvas upload.
- old file/schema compatibility.
- shell-specific resize command for control panel.
- OBJ concepts past import adapter seam.
- placeholder render modes that do not work.

### Unknown / blocker

- Electrobun WGPU input events may be insufficient for wheel/camera controls.
- Wayland screenshot/input automation needs grim/ydotool/portal adapters.
- Windows needs HWND surface + CUDA/Vulkan external-handle path.
- True Wayland-native viewport requires Electrobun Linux changes: stop forcing X11, expose `wl_surface`, use `VK_KHR_wayland_surface`.
- ovrtx runtime setup still needs packaging polish.

## Implementation plan

### 0. Baseline cleanup

- Remove `any` from current TS RPC/window types.
- Keep wording precise: Wayland workstation + Xwayland-backed viewport.
- Remove stale `.git/index.lock` if no git process.
- Commit current shell spike.

Validation:

- `bun run typecheck`
- `bun run build:ui`
- `bun run verify:shell`

### 1. Native core, no viewport yet

Implement clean native modules from old behavior/reference:

- `growth`
- `import`
- `render`
- `project`
- `app`
- minimal tests
- `assets/prototypes`, `assets/HDRI`

New CMake:

- core preset: no ovrtx/Vulkan.
- desktop/native preset: ovrtx/Vulkan.
- no wx.
- no old frontend build.

Validation:

- `cmake --preset core`
- `ctest --preset core`

### 2. Native C ABI for Bun

Build `libtoi_native_core.so`.

Expose coarse calls:

- `toi_create(options_json)`
- `toi_destroy(handle)`
- `toi_handle_command(handle, request_json)`
- `toi_free_string(ptr)`
- later: `toi_attach_x11_viewport(handle, x_window, width, height)`
- later: `toi_poll_event(handle)`

Use JSON for app commands first. It matches the app command seam and avoids per-object FFI churn.

### 3. Typed Electrobun bridge

Bun owns shell adapter only:

- UI -> Bun typed RPC.
- Bun -> native C ABI.
- no duplicated Project/growth state in TS.

Port command surface:

- `app.get_state`
- `module.*`
- `plant_types.*`
- `inspect.snapshot`

Rules:

- no `any`.
- explicit request/response types.
- runtime validation only at external boundaries.

### 4. Solid UI

Rebuild from old UI behavior, not old component structure:

- Top bar + workspace previews.
- Module workspace.
- Source: Branch module prototype + Plant type.
- Development slider.
- Growth summary.
- Prototype inspector.
- Plant type manager.
- Appearance themes.

Cleanup:

- pure DOM/CSS panel resize.
- preserve `undefined` = not loaded, `[]` = loaded empty.
- omit unused placeholders until real.

### 5. Vulkan surface proof

Implement:

- `x11_vulkan_surface`.
- `NativeSurfaceHandle = x_display, x_window, width, height`.
- open display in native core.
- query X window size in C++ instead of trusting CSS rect.

Acceptance:

- attach to Electrobun WGPU XID.
- create `VkSurfaceKHR`.
- present clear/test pattern.
- handle resize/swapchain recreate.

### 6. ovrtx Growth preview

Rework from old implementation:

- ovrtx session.
- CUDA/Vulkan interop.
- Vulkan presenter.
- overlay shaders.
- render thread logic from old viewport panel, minus wx.

Native core owns:

- ovrtx session.
- render loop.
- camera.
- Vulkan presenter.
- pending stage/preferences queue.

Acceptance:

- first ovrtx CUDA frame presented through Vulkan.
- age slider updates mesh points without stage reload blink.
- `verify:shell` sees viewport + native core ready.

### 7. Viewport controls in left panel

Preserve controls/concepts:

- guides visible.
- world-origin axes.
- HDRI backdrop visible.
- HDRI environment library.
- active HDRI.
- render mode = RTX only initially; omit/disable others until real.

### 8. Camera interaction

Initial:

- preserve orbit/dolly behavior in native core.
- poll pointer/buttons with `XQueryPointer` against WGPU XID for drag.
- left drag = orbit.
- right drag = dolly.

Risk:

- wheel may need Electrobun WGPU input event patch or DOM passthrough overlay.
- Patch Electrobun only after observed blocker.

### 9. Automation / Playwright-like seam

Add `agent-control` server in Bun.

API:

- `screenshot`
- `mouse.move/down/up/click/drag`
- `key.down/up/type`
- `window.rect`
- `window.raise`
- `quit`
- `app.command`
- `ui.evaluate` later if needed

Adapters:

- screenshot: `grim` on Wayland/wlroots, portal later, x11grab fallback.
- input: `ydotool`/portal first for Wayland, xdotool only for X11/Xwayland dev.
- window: Electrobun APIs.
- app command: native command seam.

Keep screenshot-coordinate flow primary. App commands are inspection/control seam.

### 10. Validation matrix

Run relevant subset per phase:

- `bun run typecheck`
- `bun run build:ui`
- `bun run verify:shell`
- `cmake --preset core && ctest --preset core`
- native viewport test pattern.
- ovrtx first-frame smoke.
- agent-control screenshot + click + quit.
- plant type create/update/delete.
- age scrub no flicker.
- HDRI missing-file error path.

## Main risks

- Electrobun WGPU input events are thin; camera wheel may need patch.
- Wayland screenshots via x11grab are insufficient; use `grim`/portal.
- Bun FFI memory ownership must be explicit.
- ovrtx runtime/package setup still manual-ish.
- Windows needs separate HWND/Vulkan/CUDA external-handle work later.
