// Native GPU viewport host. Fills the region beside the control panel and hands
// its native window handle (the X11 XID on Linux) to the Bun shell once ready,
// so the native core can attach a Vulkan surface to it.
//
// The element is sized with an inline style: Electrobun injects an unlayered
// `electrobun-wgpu { width:800px; height:300px }` rule, and Tailwind utilities
// live in cascade layers (which unlayered rules outrank), so only an inline
// style reliably makes the view fill its pane. Electrobun's own sync controller
// then tracks this element's box and resizes the native window to match.

import { createSignal, onCleanup, onMount } from "solid-js";
import type { Rect } from "../../shared/shellRpc";
import { notifyViewportReady, notifyViewportResize, reportUiEvent } from "../shell";

const FILL_STYLE = { position: "absolute", top: "0", left: "0", width: "100%", height: "100%" } as const;
const RESIZE_SETTLE_MS = 150;

export function Viewport() {
	const [status, setStatus] = createSignal("waiting for native viewport");
	let element: WgpuTagElement | undefined;

	onMount(() => {
		let listenerAttached = false;
		let disposed = false;
		let announcedId: number | undefined;
		let resizeTimer: number | undefined;
		let resizeObserver: ResizeObserver | undefined;
		let lastPixelSize = "";
		const handleReady = (event: CustomEvent<{ id: number }>) => {
			void announce(event.detail.id);
		};

		void attach();

		onCleanup(() => {
			disposed = true;
			if (listenerAttached) {
				element?.off("ready", handleReady);
			}
			resizeObserver?.disconnect();
			if (resizeTimer !== undefined) {
				window.clearTimeout(resizeTimer);
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
			if (element === undefined || announcedId === id) {
				return;
			}
			announcedId = id;
			// Let layout and Electrobun's native child resize settle before attach.
			await nextFrame();
			await nextFrame();
			if (disposed || element === undefined) {
				return;
			}
			const rect = rectOf(element);
			setStatus(`native view ${id}`);
			try {
				const result = await notifyViewportReady(id, rect);
				if (result.ok) {
					lastPixelSize = pixelSizeKey(rect);
				}
				setStatus(result.ok ? `${pixelSizeLabel(rect)} · handle ${result.nativeHandle}` : "native handle unavailable");
			} catch (cause) {
				reportUiEvent("viewport-attach-failed", { message: String(cause) });
				setStatus("native viewport attach failed");
			}
		}

		function scheduleResize() {
			if (announcedId === undefined) {
				return;
			}
			if (resizeTimer !== undefined) {
				window.clearTimeout(resizeTimer);
			}
			resizeTimer = window.setTimeout(() => {
				resizeTimer = undefined;
				void resize();
			}, RESIZE_SETTLE_MS);
		}

		async function resize() {
			if (disposed || element === undefined || announcedId === undefined) {
				return;
			}
			const rect = rectOf(element);
			const pixelSize = pixelSizeKey(rect);
			if (pixelSize === lastPixelSize) {
				return;
			}
			try {
				const result = await notifyViewportResize(announcedId, rect);
				if (!result.ok) {
					throw new Error(result.error ?? "native viewport resize failed");
				}
				lastPixelSize = `${result.width}x${result.height}`;
				setStatus(`${result.width}×${result.height} · native`);
			} catch (cause) {
				reportUiEvent("viewport-resize-failed", { message: String(cause), rect });
				setStatus("native viewport resize failed");
			}
		}
	});

	return (
		<div class="relative min-w-0 flex-1 bg-black">
			<electrobun-wgpu id="native-viewport" style={FILL_STYLE} ref={(el) => (element = el as WgpuTagElement)} />
			<div class="pointer-events-none absolute bottom-3 right-3 rounded-md bg-black/40 px-2 py-1 font-mono text-[11px] text-muted-foreground">
				{status()}
			</div>
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

function pixelSizeLabel(rect: Rect): string {
	return pixelSizeKey(rect).replace("x", "×");
}

function nextFrame(): Promise<void> {
	return new Promise((resolve) => requestAnimationFrame(() => resolve()));
}
