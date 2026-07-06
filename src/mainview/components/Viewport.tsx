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
import { notifyViewportReady, reportUiEvent } from "../shell";

const FILL_STYLE = { position: "absolute", top: "0", left: "0", width: "100%", height: "100%" } as const;

export function Viewport() {
	const [status, setStatus] = createSignal("waiting for native viewport");
	let element: WgpuTagElement | undefined;

	onMount(() => {
		let attached = false;
		let disposed = false;
		const handleReady = (event: CustomEvent<{ id: number }>) => {
			void announce(event.detail.id);
		};

		void attach();

		onCleanup(() => {
			disposed = true;
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
			// Let layout settle so the native window is sized to the full pane.
			await nextFrame();
			await nextFrame();
			if (disposed || element === undefined) {
				return;
			}
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

function nextFrame(): Promise<void> {
	return new Promise((resolve) => requestAnimationFrame(() => resolve()));
}
