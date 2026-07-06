// Native GPU viewport host. Fills the region beside the control panel and hands
// its native window handle (the X11 XID on Linux) to the Bun shell once ready,
// so the native core can attach a Vulkan surface to it.

import { createSignal, onCleanup, onMount } from "solid-js";
import type { Rect } from "../../shared/shellRpc";
import { notifyViewportReady, notifyViewportResized, reportUiEvent } from "../shell";

export function Viewport() {
	const [status, setStatus] = createSignal("waiting for native viewport");
	let element: WgpuTagElement | undefined;
	let viewId: number | undefined;

	onMount(() => {
		let attached = false;
		let disposed = false;
		let resizeScheduled = false;
		const handleReady = (event: CustomEvent<{ id: number }>) => {
			void announce(event.detail.id);
		};

		void attach();

		// Keep the native child window (and its Vulkan swapchain) aligned with the
		// DOM pane as the window and panel resize.
		const observer = new ResizeObserver(() => {
			if (viewId === undefined || element === undefined || resizeScheduled) {
				return;
			}
			resizeScheduled = true;
			requestAnimationFrame(() => {
				resizeScheduled = false;
				if (viewId !== undefined && element !== undefined) {
					void notifyViewportResized(viewId, rectOf(element));
				}
			});
		});
		if (element) observer.observe(element);

		onCleanup(() => {
			disposed = true;
			observer.disconnect();
			if (attached) {
				element?.off("ready", handleReady);
			}
		});

		async function attach() {
			await customElements.whenDefined("electrobun-wgpu");
			if (disposed || element === undefined) {
				return;
			}
			element.on("ready", handleReady);
			attached = true;
			if (element.wgpuViewId !== null) {
				void announce(element.wgpuViewId);
			}
		}

		async function announce(id: number) {
			if (element === undefined) {
				return;
			}
			// Let flex layout settle so the reported rect is the full pane, not a
			// transient size — the native window is sized to this rect.
			await nextFrame();
			await nextFrame();
			if (disposed || element === undefined) {
				return;
			}
			viewId = id;
			setStatus(`native view ${id}`);
			try {
				const result = await notifyViewportReady(id, rectOf(element));
				setStatus(result.ok ? `handle ${result.nativeHandle}` : "native handle unavailable");
			} catch (cause) {
				reportUiEvent("viewport-attach-failed", { message: String(cause) });
				setStatus("native viewport attach failed");
			}
		}
	});

	return (
		<div class="relative min-w-0 flex-1 bg-black">
			<electrobun-wgpu
				id="native-viewport"
				class="absolute inset-0 h-full w-full"
				ref={(el) => (element = el as WgpuTagElement)}
			/>
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

function nextFrame(): Promise<void> {
	return new Promise((resolve) => requestAnimationFrame(() => resolve()));
}
