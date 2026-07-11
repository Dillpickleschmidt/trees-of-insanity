// Native GPU viewport host. Fills the region beside the control panel and hands
// its native window handle (the X11 XID on Linux) to the Bun shell once ready,
// so the native core can attach a Vulkan surface to it.
//
// The element is sized with an inline style: Electrobun injects an unlayered
// `electrobun-wgpu { width:800px; height:300px }` rule, and Tailwind utilities
// live in cascade layers (which unlayered rules outrank), so only an inline
// style reliably makes the view fill its pane. Electrobun's own sync controller
// then tracks this element's box and resizes the native window to match.

import { createEffect, on, onCleanup, onMount } from "solid-js";
import type { Rect } from "../../shared/shellRpc";
import {
	notifyViewportDetach,
	notifyViewportReady,
	notifyViewportResize,
	notifyViewportSurfaceChanged,
	reportUiEvent,
	sendViewportCameraInput,
} from "../shell";

const FILL_STYLE = { position: "absolute", top: "0", left: "0", width: "100%", height: "100%" } as const;
const RESIZE_SETTLE_MS = 150;

export function Viewport(props: { hidden: boolean }) {
	let host: HTMLDivElement | undefined;
	let element: WgpuTagElement | undefined;

	onMount(() => {
		let listenerAttached = false;
		let disposed = false;
		let attachedId: number | undefined;
		let attachingId: number | undefined;
		let resizeTimer: number | undefined;
		let resizeObserver: ResizeObserver | undefined;
		let lastPixelSize = "";
		let activePointerId: number | undefined;
		let activeCameraInput: "orbit" | "pan" | "dolly" | undefined;
		let lastPointerX = 0;
		let lastPointerY = 0;
		let pendingPointerDx = 0;
		let pendingPointerDy = 0;
		let cameraInputFrame: number | undefined;

		createEffect(on(() => props.hidden, () => {
			element?.syncDimensions(true);
			if (attachedId !== undefined) {
				notifyViewportSurfaceChanged(attachedId);
			}
		}));

		const handleReady = (event: CustomEvent<{ id: number }>) => {
			void announce(event.detail.id);
		};

		host?.addEventListener("pointerdown", handlePointerDown);
		host?.addEventListener("pointermove", handlePointerMove);
		host?.addEventListener("pointerup", endPointerInput);
		host?.addEventListener("pointercancel", endPointerInput);
		host?.addEventListener("wheel", handleWheel, { passive: false });
		host?.addEventListener("contextmenu", preventDefault);
		void attach();

		onCleanup(() => {
			disposed = true;
			host?.removeEventListener("pointerdown", handlePointerDown);
			host?.removeEventListener("pointermove", handlePointerMove);
			host?.removeEventListener("pointerup", endPointerInput);
			host?.removeEventListener("pointercancel", endPointerInput);
			host?.removeEventListener("wheel", handleWheel);
			host?.removeEventListener("contextmenu", preventDefault);
			if (listenerAttached) {
				element?.off("ready", handleReady);
			}
			if (attachedId !== undefined) {
				notifyViewportDetach(attachedId);
			}
			resizeObserver?.disconnect();
			if (resizeTimer !== undefined) {
				window.clearTimeout(resizeTimer);
			}
			if (cameraInputFrame !== undefined) {
				cancelAnimationFrame(cameraInputFrame);
			}
		});

		async function attach() {
			await customElements.whenDefined("electrobun-wgpu");
			if (disposed || element === undefined) {
				return;
			}
			element.on("ready", handleReady);
			listenerAttached = true;
			resizeObserver = new ResizeObserver(scheduleResize);
			resizeObserver.observe(element);
			if (element.wgpuViewId !== null) {
				void announce(element.wgpuViewId);
			}
		}

		async function announce(id: number) {
			if (element === undefined || attachedId === id || attachingId === id) {
				return;
			}
			attachingId = id;
			try {
				// Let layout and Electrobun's native child resize settle before attach.
				await nextFrame();
				await nextFrame();
				if (disposed || element === undefined) {
					return;
				}
				const rect = rectOf(element);
				const result = await notifyViewportReady(id, rect);
				if (!result.ok) {
					throw new Error(result.error ?? "native viewport attach failed");
				}
				if (disposed) {
					notifyViewportDetach(id);
					return;
				}
				attachedId = id;
				lastPixelSize = pixelSizeKey(rect);
			} catch (cause) {
				reportUiEvent("viewport-attach-failed", { message: String(cause) });
			} finally {
				if (attachingId === id) {
					attachingId = undefined;
				}
			}
		}

		function scheduleResize() {
			if (attachedId === undefined) {
				return;
			}
			notifyViewportSurfaceChanged(attachedId);
			if (resizeTimer !== undefined) {
				window.clearTimeout(resizeTimer);
			}
			resizeTimer = window.setTimeout(() => {
				resizeTimer = undefined;
				void resize();
			}, RESIZE_SETTLE_MS);
		}

		function handlePointerDown(event: PointerEvent) {
			if (host === undefined || attachedId === undefined || activePointerId !== undefined) {
				return;
			}
			const kind = event.button === 0 ? "orbit" : event.button === 1 ? "pan" : event.button === 2 ? "dolly" : undefined;
			if (kind === undefined) {
				return;
			}
			event.preventDefault();
			activePointerId = event.pointerId;
			activeCameraInput = kind;
			lastPointerX = event.clientX;
			lastPointerY = event.clientY;
			host.setPointerCapture(event.pointerId);
		}

		function handlePointerMove(event: PointerEvent) {
			if (event.pointerId !== activePointerId || activeCameraInput === undefined || attachedId === undefined) {
				return;
			}
			const dx = event.clientX - lastPointerX;
			const dy = event.clientY - lastPointerY;
			lastPointerX = event.clientX;
			lastPointerY = event.clientY;
			pendingPointerDx += dx;
			pendingPointerDy += dy;
			cameraInputFrame ??= requestAnimationFrame(flushCameraInput);
		}

		function flushCameraInput() {
			cameraInputFrame = undefined;
			if (activeCameraInput === undefined || attachedId === undefined) {
				pendingPointerDx = 0;
				pendingPointerDy = 0;
				return;
			}
			const dx = pendingPointerDx;
			const dy = pendingPointerDy;
			pendingPointerDx = 0;
			pendingPointerDy = 0;
			if (dx === 0 && dy === 0) {
				return;
			}
			sendViewportCameraInput(attachedId, {
				kind: activeCameraInput,
				dx,
				dy,
				viewportHeight: Math.max(1, Math.trunc(host?.clientHeight ?? 1)),
			});
		}

		function endPointerInput(event: PointerEvent) {
			if (event.pointerId !== activePointerId) {
				return;
			}
			if (cameraInputFrame !== undefined) {
				cancelAnimationFrame(cameraInputFrame);
				flushCameraInput();
			}
			if (host?.hasPointerCapture(event.pointerId)) {
				host.releasePointerCapture(event.pointerId);
			}
			activePointerId = undefined;
			activeCameraInput = undefined;
		}

		function handleWheel(event: WheelEvent) {
			if (attachedId === undefined) {
				return;
			}
			event.preventDefault();
			const pixels = event.deltaMode === WheelEvent.DOM_DELTA_LINE
				? event.deltaY * 16
				: event.deltaMode === WheelEvent.DOM_DELTA_PAGE
					? event.deltaY * Math.max(1, host?.clientHeight ?? 1)
					: event.deltaY;
			sendViewportCameraInput(attachedId, {
				kind: "wheel",
				dx: 0,
				dy: -pixels / 100,
				viewportHeight: Math.max(1, Math.trunc(host?.clientHeight ?? 1)),
			});
		}

		function preventDefault(event: Event) {
			event.preventDefault();
		}

		async function resize() {
			if (disposed || element === undefined || attachedId === undefined) {
				return;
			}
			const rect = rectOf(element);
			const pixelSize = pixelSizeKey(rect);
			if (pixelSize === lastPixelSize) {
				return;
			}
			try {
				const result = await notifyViewportResize(attachedId, rect);
				if (!result.ok) {
					throw new Error(result.error ?? "native viewport resize failed");
				}
				lastPixelSize = pixelSize;
			} catch (cause) {
				reportUiEvent("viewport-resize-failed", { message: String(cause), rect });
			}
		}
	});

	return (
		<div class="relative min-w-0 flex-1 bg-black" ref={(el) => (host = el)}>
			<electrobun-wgpu
				id="native-viewport"
				passthrough
				style={{ ...FILL_STYLE, transform: props.hidden ? "translateX(-200vw)" : "none" }}
				ref={(el) => (element = el as WgpuTagElement)}
			/>
		</div>
	);
}

function rectOf(element: HTMLElement): Rect {
	const rect = element.getBoundingClientRect();
	return { x: rect.x, y: rect.y, width: rect.width, height: rect.height };
}

function pixelSizeKey(rect: Rect): string {
	return `${Math.max(1, Math.trunc(rect.width))}x${Math.max(1, Math.trunc(rect.height))}`;
}

function nextFrame(): Promise<void> {
	return new Promise((resolve) => requestAnimationFrame(() => resolve()));
}
