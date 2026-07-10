// Shell singletons for the main view: the Electrobun RPC channel, the typed
// application-command client, and small helpers over the shell's RPC surface.
// Created once and shared by every component.

import { Electroview } from "electrobun/view";
import type { Rect, ShellRpcSchema, ViewportCameraInput, ViewportStatus } from "../shared/shellRpc";
import { createAppClient } from "./appClient";

const rpc = Electroview.defineRPC<ShellRpcSchema>({
	maxRequestTime: 10_000,
	handlers: {
		requests: {},
		messages: {},
	},
});

new Electroview({ rpc });

export const appClient = createAppClient((request) => rpc.request.appCommand(request));

export function reportUiEvent(type: string, data?: Record<string, unknown>) {
	rpc.send.uiEvent({ type, data });
}

export function notifyViewportReady(id: number, rect: Rect) {
	return rpc.request.viewportReady({ id, rect });
}

export function notifyViewportResize(id: number, rect: Rect) {
	return rpc.request.viewportResize({ id, rect });
}

export function notifyViewportDetach(id: number) {
	rpc.send.viewportDetach({ id });
}

export function notifyViewportSurfaceChanged(id: number) {
	rpc.send.viewportSurfaceChanged({ id });
}

export function sendViewportCameraInput(id: number, input: ViewportCameraInput) {
	rpc.send.viewportCameraInput({ id, input });
}

export function onViewportStatus(listener: (status: ViewportStatus) => void) {
	rpc.addMessageListener("viewportStatus", listener);
	return () => rpc.removeMessageListener("viewportStatus", listener);
}
