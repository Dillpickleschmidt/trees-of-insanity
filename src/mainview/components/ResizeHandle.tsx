import { createSignal } from "solid-js";

// Divider between the control panel and the native viewport. Pure DOM resize:
// reports the desired panel width (the pointer's x, panel origin is the window
// left edge); the app clamps and applies it.
export function ResizeHandle(props: { onResize: (panelWidthCss: number) => void }) {
	const [dragging, setDragging] = createSignal(false);
	let raf = 0;
	let pendingWidth = 0;
	const flush = () => {
		raf = 0;
		props.onResize(pendingWidth);
	};
	const onMove = (event: MouseEvent) => {
		pendingWidth = Math.max(1, Math.round(event.clientX));
		// Coalesce moves to one update per frame.
		if (!raf) raf = requestAnimationFrame(flush);
	};
	const onUp = () => {
		setDragging(false);
		document.removeEventListener("mousemove", onMove);
		document.removeEventListener("mouseup", onUp);
		document.body.style.cursor = "";
		document.body.style.userSelect = "";
		if (raf) {
			cancelAnimationFrame(raf);
			raf = 0;
		}
	};
	const onDown = (event: MouseEvent) => {
		event.preventDefault();
		setDragging(true);
		// Keep the resize cursor and suppress text selection for the whole drag.
		document.body.style.cursor = "col-resize";
		document.body.style.userSelect = "none";
		document.addEventListener("mousemove", onMove);
		document.addEventListener("mouseup", onUp);
	};
	return (
		<div
			role="separator"
			aria-orientation="vertical"
			aria-label="Resize panel"
			title="Drag to resize"
			class="group absolute right-0 top-0 z-40 flex h-full w-2 translate-x-1/2 cursor-col-resize items-center justify-center"
			onMouseDown={onDown}
		>
			<span
				class="h-10 w-[3px] rounded-full transition-colors"
				classList={{
					"bg-muted-foreground/70": dragging(),
					"bg-muted-foreground/25 group-hover:bg-muted-foreground/70": !dragging(),
				}}
			/>
		</div>
	);
}
