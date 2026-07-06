// Shell singletons for the main view: the Electrobun RPC channel, the typed
// application-command client, and small helpers over the shell's RPC surface.
// Created once and shared by every component.

import { Electroview } from "electrobun/view";
import type { Rect, ShellRpcSchema } from "../shared/shellRpc";
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
	void rpc.request.uiEvent({ type, data }).catch(() => {});
}

export function notifyViewportReady(id: number, rect: Rect) {
	return rpc.request.viewportReady({ id, rect });
}
