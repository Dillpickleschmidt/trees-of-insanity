# ovrtx render-completion synchronization — investigation & spec

Status: investigation complete; current fix is `cudaDeviceSynchronize()`. A tighter,
targeted sync is **not achievable with the ovrtx 0.3.0 API as exposed** (see Findings).
Revisit if ovrtx is upgraded or NVIDIA clarifies the device-map read contract.

Version note: **0.3.0 is the latest ovrtx release** (checked 2026-07-06 —
`NVIDIA-Omniverse/ovrtx` has only pre-releases 0.1.0 → 0.2.0 → 0.3.0, v0.3.0 dated
2026-05-18). So the "newer ovrtx" avenue below is future-dependent, not an available
upgrade — there is nothing to bump to today. It is also still pre-release software,
consistent with the undocumented device-map sync contract and the header comments that
reference `ovrtx_signal_event`/`ovrtx_wait_all_events` (not actually declared in 0.3.0).

## Problem

The growth preview flickered to black while re-rendering rapidly (e.g. moving the
camera). Root cause: **ovrtx runs its RTX render asynchronously and
`render_cuda_frame` (via `ovrtx_step` / `ovrtx_fetch_results` / `ovrtx_map_render_var_output`)
returns before that render has finished on the GPU.** The viewport then copies the
mapped `LdrColor` on our CUDA stream and races the render, blitting a partly- or
un-rendered (black) frame. `DistanceToCameraSD` is geometric and lands early, so
occlusion stayed correct — only the color darkened.

Current fix (`renderer_session.cpp`, commit 66c8214): `cudaDeviceSynchronize()` after
mapping, so a returned frame is guaranteed complete. Correct and verified
(0/25 black under continuous render). Its only downside is that it is a **full-device
wait** — it serializes all CUDA work, preventing overlap of ovrtx's render with the
previous frame's Vulkan-interop CUDA work. For our on-demand render loop this cost is
negligible (ovrtx render dominates frame time and we only render on change), but the
"correct" long-term shape is to wait on **just** ovrtx's render, not the whole device.

## What the ovrtx API exposes (0.3.0.312915)

From `include/ovrtx/ovrtx.h` / `ovrtx_types.h`:

- `ovrtx_fetch_results` / `ovrtx_map_render_var_output` docs both state: *"The complete
  production of render outputs is not determined by the completion of the asynchronous
  `ovrtx_step()` operation within the stream, so it is not possible to ensure this
  operation returns the results immediately by waiting for a stream synchronization event
  signaled after the `ovrtx_step()` operation."* → **you cannot gate render completion on
  the step's stream.**
- `ovrtx_map_output_description_t.sync_stream`: *"Providing a stream here means that after
  the map call returns the output data can immediately be accessed on a cuda stream that
  is synchronized to the provided stream."* → the intended device-read sync knob.
- The mapped `ovrtx_render_var_output_t.cuda_sync` (`{uintptr_t stream; uintptr_t wait_event;}`):
  *"Single sync covering all tensors"*; the map doc says the result *"can contain cuda
  events that must be synchronized to before actually accessing the output memory."*
- `ovrtx_signal_event()` / `ovrtx_wait_all_events()` are **referenced in a doc comment but
  not declared as functions** in 0.3.0 — not an available avenue here.

### Example-code evidence
- The only CUDA-**device** map example (`tests/docs/c/test_camera_sensors.cpp`,
  `[snippet:doc-map-render-output-cuda-c]`) sets `map_desc.sync_stream = 1` (the CUDA
  default stream), checks the tensor, and **unmaps without ever reading the pixels**.
- Every example that actually **reads** output data (`test_camera_aovs.cpp`, etc.) maps
  with `OVRTX_MAP_DEVICE_TYPE_CPU`, which the docs say *"will incur synchronization and
  copy"* — i.e. ovrtx synchronizes internally for CPU maps.
- ⇒ There is **no worked example of a device-map async read + explicit sync**, which is
  exactly the (performance-motivated) path we use (`OVRTX_MAP_DEVICE_TYPE_CUDA` for
  `LdrColor`, `..._CUDA_ARRAY` for `DistanceToCameraSD`).

## Data gathered

Instrumented `render_cuda_frame_once` to log the returned `cuda_sync` for both maps
(single render via the ovrtx smoke test):

```
ldr    sync_stream=0x<ourStream> cuda_sync.stream=0x0 wait_event=0x<nonzero> status=1
dist   sync_stream=0x<ourStream> cuda_sync.stream=0x0 wait_event=0x<nonzero> status=1
```
- `cuda_sync.stream == 0` (ovrtx injected no stream dependency); `wait_event` is a
  non-zero CUDA event; `LdrColor` and `DistanceToCameraSD` share the same event per step.

Fix candidates, measured in the running app under **forced continuous render** with
**dense** screen sampling (grim every ~0.35 s, "black" = mean viewport luma < 30):

| Approach (before the copy)                                   | Black frames | Verdict |
|--------------------------------------------------------------|--------------|---------|
| none (baseline)                                              | ~15–17 / 20  | flicker |
| `cudaStreamSynchronize(sync_stream)`                         | 15 / 20      | no fix (render is on a different stream) |
| `cudaStreamWaitEvent(sync_stream, cuda_sync.wait_event)`     | 22 / 25      | no fix |
| `cudaEventSynchronize(cuda_sync.wait_event)`                 | 25 / 25      | no fix |
| `std::this_thread::sleep_for(10ms)` (pacing only)            | 0 / 20       | masks, not a fix |
| **`cudaDeviceSynchronize()`**                                | **0 / 25**   | **fix (shipped)** |

Note: **sparse sampling lies** — at 1 s intervals several of the "no fix" approaches
looked stable. Always dense-sample and count black frames.

Orthogonal, also confirmed here: Vulkan **synchronization validation** (`vulkan-validation-layers`,
`VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT`) is **clean** for this bug —
it is entirely CUDA-side and invisible to Vulkan. (It did surface a *separate* real hazard,
the swapchain WAR-on-acquire, fixed in commit 60fb64f.)

## Findings

1. The render is async on an ovrtx-internal stream that is **not** `sync_stream`, so
   `cudaStreamSynchronize(sync_stream)` cannot gate it.
2. The returned `cuda_sync.wait_event` **does not gate render completion** — neither a
   GPU-side wait (`cudaStreamWaitEvent`) nor a CPU-side wait (`cudaEventSynchronize`) on it
   prevents the black frames. Most likely the event is either recorded before the render
   truly completes, or reused across frames so the value we read reflects a prior frame's
   recording (a classic event-reuse race). Either way it is not usable as a
   per-frame render-done fence as exposed.
3. The documented `sync_stream` contract ("access on a stream synchronized to it") did
   **not** hold for our custom (non-default) stream with an immediate device read.
4. Only a full-device wait reliably gates the render. Hence the shipped fix.

## Spec — path to a targeted (non-device-wide) sync

Prerequisite: **one** of the following must be true; none is today.

- **A. A per-frame render-done event we can actually wait on.** Requires ovrtx to
  guarantee `cuda_sync.wait_event` is (a) freshly recorded for *this* step and (b)
  recorded only *after* the render's last kernel, before `map` returns. Verify by:
  record the event value per frame + `cudaEventQuery` it immediately after map (expect
  `cudaErrorNotReady` until the render lands). If that holds on a newer ovrtx, the fix is:
  `cudaStreamWaitEvent(consumer_stream, wait_event, 0)` right after each map — fully async,
  no device stall.
- **B. Honor the `sync_stream` contract.** If a newer ovrtx makes `sync_stream` truly wait
  for the render (test: `map_desc.sync_stream = ourStream`, then `cudaStreamSynchronize(ourStream)`
  → expect 0/25 black), then no extra call is needed at all; consuming on `sync_stream`
  is safe by contract.
- **C. Default-stream variant.** The one device-map example uses `sync_stream = 1`. Worth a
  single experiment: map with `sync_stream = 1` and make our consumer stream wait on the
  default stream. Low confidence, but cheap.
- **D. Ask NVIDIA / read newer ovrtx release notes** for the canonical
  "map to CUDA device memory and read on my own stream" synchronization pattern. The
  0.3.0 examples do not demonstrate it.

Recommendation: **keep `cudaDeviceSynchronize()`** until one of A–D is confirmed on the
ovrtx version in use. When revisiting, drive the decision with the experiment harness
below rather than eyeballing.

## Update: compared against NVIDIA's official `vulkan-interop` example

Reviewed `NVIDIA-Omniverse/ovrtx` `examples/c/vulkan-interop` (main). It **is** fully async
with **no per-frame device sync**, so "not achievable" was too strong — but it gets there
through a whole architecture we don't share, and a *partial* adaptation into ours does not
work. The example's recipe:
- `map_desc.sync_stream = 0` (no stream sync; rely on the returned event).
- `cudaStreamWaitEvent(consumer_stream, cuda_sync.wait_event)` before the copy (GPU-orders
  copy-after-render), copy on that stream, then signal a **timeline semaphore** for the
  CUDA→Vulkan handoff.
- **Double-buffered**: Vulkan samples the PREVIOUS frame's buffer (already timeline-signaled,
  hence complete) while CUDA writes the next — Vulkan never reads the buffer being written.
- Default **async** render mode (no `sync_mode`); it also skips `ovrtx_wait_op` on the step
  (fetch_results only). It **samples** a fullscreen quad rather than blitting.

Tested adapting just the event-wait into our wrapper, forced continuous render, dense-sampled:

| Adaptation | Black frames |
|---|---|
| `sync_stream=0` + `cudaStreamWaitEvent(stream, wait_event)`, no device sync | 24 / 25 |
| same, plus `sync_mode=false` (async, like the example) | 25 / 25 |
| `cudaDeviceSynchronize()` (shipped) | 0 / 25 |

Conclusion: the returned `wait_event` does **not** act as a render-completion fence in our
single-buffered, same-frame-read flow, regardless of `sync_stream` (0 vs ours) or `sync_mode`
(true vs false). The example avoids the race chiefly via **double-buffering** (it reads a
frame-old, already-complete buffer), not the event alone. Reaching the async ideal here would
mean adopting the example's double-buffer + timeline-semaphore pipeline — a real rewrite — and
since our on-demand loop only renders on a change (not continuously), the `cudaDeviceSynchronize`
stall is negligible in practice. Not worth it now; **keep the device sync**. This supersedes
options A/B/C above for 0.3.0: A/B are what the event/stream *should* do but empirically don't
here; the real lever is double-buffering, not a newer ovrtx.

### Regression / verification harness (reusable)
- Temporarily force `needs_render = true` in `record_growth_frame` (continuous render).
- `bun run build:native && electrobun dev`; wait for `viewport attached` (retry once for
  the known Bun 1.3.13 startup crash, `terminated by signal: 4`).
- Dense-sample: `grim`, crop the viewport region, compute mean luma, ~0.35 s apart, ≥20
  samples; count frames with mean < 30. A real fix is **0 black**; sparse sampling is not
  trustworthy.
- Confirm no regression to Vulkan sync via the validation layer run described above.

## References
- `src/toi/ovrtx/src/renderer_session.cpp` — `render_cuda_frame_once` (the sync site).
- ovrtx 0.3.0 headers: `include/ovrtx/ovrtx.h`, `include/ovrtx/ovrtx_types.h`
  (`ovrtx_map_output_description_t`, `ovrtx_render_var_output_t`, `ovrtx_cuda_sync_t`).
- ovrtx device-map example: `tests/docs/c/test_camera_sensors.cpp`.
- Commits: 66c8214 (device-sync fix), 60fb64f (swapchain WAR-on-acquire), 81d8ff8 (on-demand render).
