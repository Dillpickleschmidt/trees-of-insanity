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
	id: number;
	nativeHandle: string | null;
};

export type ViewportResizeParams = {
	id: number;
	rect: Rect;
};

export type ViewportResizeResult = {
	ok: boolean;
	id: number;
	width: number;
	height: number;
	error?: string;
};

export type UiEventParams = {
	type: string;
	data?: Record<string, unknown>;
};

export type UiEventResult = {
	ok: true;
};

type EmptyRequests = Record<never, { params: never; response: never }>;
type EmptyMessages = Record<never, never>;

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
			uiEvent: {
				params: UiEventParams;
				response: UiEventResult;
			};
			appCommand: {
				params: AppCommandParams;
				response: AppCommandResult;
			};
		};
		messages: EmptyMessages;
	}>;
	webview: RPCSchema<{
		requests: EmptyRequests;
		messages: EmptyMessages;
	}>;
};
