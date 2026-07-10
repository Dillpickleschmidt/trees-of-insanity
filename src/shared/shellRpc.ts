import type { RPCSchema } from "electrobun/bun";
import type { CommandRequest, CommandResponse } from "./appCommands";

export type Rect = {
	x: number;
	y: number;
	width: number;
	height: number;
};

export type AppCommandParams = CommandRequest;
export type AppCommandResult = CommandResponse;

export type ViewportReadyParams = {
	id: number;
	rect: Rect;
};

export type ViewportReadyResult = {
	ok: boolean;
	error?: string;
};

export type ViewportPhase = "detached" | "starting" | "warming" | "rendering" | "ready" | "resizing" | "error";

export type ViewportExtent = {
	width: number;
	height: number;
};

export type ViewportStatus = {
	phase: ViewportPhase;
	message: string;
	swapchain: ViewportExtent;
	color: ViewportExtent;
	depth: ViewportExtent | null;
	frame_generation: number;
};

export type ViewportResizeParams = {
	id: number;
	rect: Rect;
};

export type ViewportCameraInput = {
	kind: "orbit" | "pan" | "dolly" | "wheel";
	dx: number;
	dy: number;
	viewportHeight: number;
};

export type ViewportResizeResult = {
	ok: boolean;
	error?: string;
};

export type UiEventParams = {
	type: string;
	data?: Record<string, unknown>;
};

type EmptyRequests = Record<never, { params: never; response: never }>;

export type ShellRpcSchema = {
	bun: RPCSchema<{
		requests: {
			viewportReady: {
				params: ViewportReadyParams;
				response: ViewportReadyResult;
			};
			viewportResize: {
				params: ViewportResizeParams;
				response: ViewportResizeResult;
			};
			appCommand: {
				params: AppCommandParams;
				response: AppCommandResult;
			};
		};
		messages: {
			uiEvent: UiEventParams;
			viewportDetach: { id: number };
			viewportSurfaceChanged: { id: number };
			viewportCameraInput: { id: number; input: ViewportCameraInput };
		};
	}>;
	webview: RPCSchema<{
		requests: EmptyRequests;
		messages: {
			viewportStatus: ViewportStatus;
		};
	}>;
};
