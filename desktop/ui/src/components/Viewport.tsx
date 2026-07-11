import { onCleanup, onMount } from "solid-js";
import { sendViewportCameraInput, updateViewportRect } from "../shell";

export function Viewport() {
	let host: HTMLDivElement | undefined;

	onMount(() => {
		let activePointerId: number | undefined;
		let activeCameraInput: "orbit" | "pan" | "dolly" | undefined;
		let lastPointerX = 0;
		let lastPointerY = 0;
		let pendingPointerDx = 0;
		let pendingPointerDy = 0;
		let cameraInputFrame: number | undefined;

		const resizeObserver = new ResizeObserver(reportRect);
		if (host !== undefined) resizeObserver.observe(host);
		host?.addEventListener("pointerdown", handlePointerDown);
		host?.addEventListener("pointermove", handlePointerMove);
		host?.addEventListener("pointerup", endPointerInput);
		host?.addEventListener("pointercancel", endPointerInput);
		host?.addEventListener("wheel", handleWheel, { passive: false });
		host?.addEventListener("contextmenu", preventDefault);
		reportRect();

		onCleanup(() => {
			resizeObserver.disconnect();
			host?.removeEventListener("pointerdown", handlePointerDown);
			host?.removeEventListener("pointermove", handlePointerMove);
			host?.removeEventListener("pointerup", endPointerInput);
			host?.removeEventListener("pointercancel", endPointerInput);
			host?.removeEventListener("wheel", handleWheel);
			host?.removeEventListener("contextmenu", preventDefault);
			if (cameraInputFrame !== undefined) cancelAnimationFrame(cameraInputFrame);
		});

		function reportRect() {
			if (host === undefined) return;
			const rect = host.getBoundingClientRect();
			updateViewportRect({ x: rect.x, y: rect.y, width: rect.width, height: rect.height });
		}

		function handlePointerDown(event: PointerEvent) {
			if (host === undefined || activePointerId !== undefined || event.target !== host) return;
			const kind = event.button === 0 ? "orbit" : event.button === 1 ? "pan" : event.button === 2 ? "dolly" : undefined;
			if (kind === undefined) return;
			event.preventDefault();
			activePointerId = event.pointerId;
			activeCameraInput = kind;
			lastPointerX = event.clientX;
			lastPointerY = event.clientY;
			host.setPointerCapture(event.pointerId);
		}

		function handlePointerMove(event: PointerEvent) {
			if (event.pointerId !== activePointerId || activeCameraInput === undefined) return;
			pendingPointerDx += event.clientX - lastPointerX;
			pendingPointerDy += event.clientY - lastPointerY;
			lastPointerX = event.clientX;
			lastPointerY = event.clientY;
			cameraInputFrame ??= requestAnimationFrame(flushCameraInput);
		}

		function flushCameraInput() {
			cameraInputFrame = undefined;
			if (activeCameraInput === undefined) {
				pendingPointerDx = 0;
				pendingPointerDy = 0;
				return;
			}
			const dx = pendingPointerDx;
			const dy = pendingPointerDy;
			pendingPointerDx = 0;
			pendingPointerDy = 0;
			if (dx === 0 && dy === 0) return;
			sendViewportCameraInput({
				kind: activeCameraInput,
				dx,
				dy,
				viewportHeight: Math.max(1, Math.trunc(host?.clientHeight ?? 1)),
			});
		}

		function endPointerInput(event: PointerEvent) {
			if (event.pointerId !== activePointerId) return;
			if (cameraInputFrame !== undefined) {
				cancelAnimationFrame(cameraInputFrame);
				flushCameraInput();
			}
			if (host?.hasPointerCapture(event.pointerId)) host.releasePointerCapture(event.pointerId);
			activePointerId = undefined;
			activeCameraInput = undefined;
		}

		function handleWheel(event: WheelEvent) {
			if (event.target !== host) return;
			event.preventDefault();
			const pixels = event.deltaMode === WheelEvent.DOM_DELTA_LINE
				? event.deltaY * 16
				: event.deltaMode === WheelEvent.DOM_DELTA_PAGE
					? event.deltaY * Math.max(1, host?.clientHeight ?? 1)
					: event.deltaY;
			sendViewportCameraInput({
				kind: "wheel",
				dx: 0,
				dy: -pixels / 100,
				viewportHeight: Math.max(1, Math.trunc(host?.clientHeight ?? 1)),
			});
		}

		function preventDefault(event: Event) {
			event.preventDefault();
		}
	});

	return <div id="viewport-host" class="relative min-w-0 flex-1 bg-transparent" ref={(element) => (host = element)} />;
}
