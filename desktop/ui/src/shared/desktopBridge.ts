export type Rect = {
	x: number;
	y: number;
	width: number;
	height: number;
};

export type ViewportCameraInputKind = "orbit" | "pan" | "dolly" | "wheel";

export type ViewportCameraInput = {
	kind: ViewportCameraInputKind;
	dx: number;
	dy: number;
	viewportHeight: number;
};

export type ViewportPhase = "detached" | "starting" | "warming" | "rendering" | "ready" | "resizing" | "error";

export type ViewportExtent = {
	width: number;
	height: number;
};

export type ViewportStatus = {
	phase: ViewportPhase;
	message: string;
	viewport: ViewportExtent;
	color: ViewportExtent;
	depth: ViewportExtent | null;
	frame_generation: number;
};
