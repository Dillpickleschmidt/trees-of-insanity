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

export type ProjectedPlantDiagnosticLabel = {
	x: number;
	y: number;
	visible: boolean;
	direct_light_exposure: number;
	accumulated_light: number;
	vigor: number;
};

export type ViewportStatus = {
	phase: ViewportPhase;
	message: string;
	viewport: ViewportExtent;
	color: ViewportExtent;
	depth: ViewportExtent | null;
	frame_generation: number;
	scene_frame_count: number;
	precomposition_count: number;
};
