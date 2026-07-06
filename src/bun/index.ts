import { BrowserView, BrowserWindow, Updater, WGPUView } from "electrobun/bun";
import { appendFileSync, mkdirSync } from "node:fs";
import { dirname } from "node:path";
import type { AppCommandParams, AppCommandResult } from "../shared/shellRpc";
import type {
	ShellRpcSchema,
	UiEventParams,
	UiEventResult,
	ViewportReadyParams,
	ViewportReadyResult,
} from "../shared/shellRpc";
import { defaultNativeCoreOptions, NativeCore } from "./nativeCore";

const DEV_SERVER_PORT = 5173;
const DEV_SERVER_URL = `http://127.0.0.1:${DEV_SERVER_PORT}`;
const USE_DEV_SERVER = process.env.TOI_USE_VITE_DEV_SERVER === "1";

let mainWindow: BrowserWindow | undefined;

writeAutomationEvent("main-starting");

const core = openNativeCore();

process.on("uncaughtException", (error) => {
	writeAutomationEvent("uncaught-exception", { message: error.message, stack: error.stack });
});
process.on("unhandledRejection", (error) => {
	writeAutomationEvent("unhandled-rejection", { message: String(error) });
});

const rpc = BrowserView.defineRPC<ShellRpcSchema>({
	maxRequestTime: 10_000,
	handlers: {
		requests: {
			uiEvent: ({ type, data }: UiEventParams): UiEventResult => {
				writeAutomationEvent(`ui:${type}`, data);
				return { ok: true };
			},
			appCommand: (request: AppCommandParams): AppCommandResult => {
				const id = request.id ?? null;
				if (core === undefined) {
					return { id, ok: false, error: "native core unavailable" };
				}
				try {
					return core.request(request);
				} catch (error) {
					writeAutomationEvent("app-command-failed", { method: request.method, message: String(error) });
					return { id, ok: false, error: String(error) };
				}
			},
			viewportReady: ({ id, rect }: ViewportReadyParams): ViewportReadyResult => {
				const view = WGPUView.getById(id) ?? WGPUView.adoptExisting(id, {
					windowId: mainWindow?.id ?? 0,
					autoResize: false,
					frame: rect,
				});
				const nativeHandle = view?.getNativeHandle() ?? null;
				const result = {
					ok: view !== undefined && nativeHandle !== null,
					id,
					nativeHandle: nativeHandle === null ? null : String(nativeHandle),
				};
				console.log(
					`native viewport ready id=${id} handle=${result.nativeHandle ?? "null"} rect=${Math.round(rect.width)}x${Math.round(rect.height)}`,
				);
				writeAutomationEvent("viewport-ready", { ...result, rect });

				if (core !== undefined && nativeHandle !== null) {
					const attach = core.attachX11Viewport(Number(nativeHandle), rect.width, rect.height);
					console.log(
						attach.ok
							? `viewport attached: ${attach.device} ${attach.width}x${attach.height}`
							: `viewport attach failed: ${attach.error}`,
					);
					writeAutomationEvent("viewport-attached", attach);
				}
				return result;
			},
		},
		messages: {},
	},
});

const url = await mainViewUrl();

mainWindow = new BrowserWindow({
	title: "Trees of Insanity",
	url,
	renderer: "native",
	frame: {
		width: 1280,
		height: 800,
		x: 160,
		y: 100,
	},
	rpc,
});

writeAutomationEvent("window-created", { windowId: mainWindow.id });
mainWindow.webview.on("dom-ready", () => {
	writeAutomationEvent("dom-ready", { webviewId: mainWindow?.webviewId });
});

console.log("Trees of Insanity shell started");
writeAutomationEvent("main-started");

startControlServer();

for (const signal of ["SIGINT", "SIGTERM"] as const) {
	process.on(signal, () => {
		core?.close();
		process.exit(0);
	});
}

// Local control channel (the app.command half of the agent-control seam):
// POST /command {method, params} -> native command seam. Enables driving and
// verifying the app without simulating OS input. Enabled via TOI_CONTROL_PORT.
function startControlServer() {
	const port = Number(process.env.TOI_CONTROL_PORT);
	if (!Number.isInteger(port) || port <= 0) {
		return;
	}
	try {
		Bun.serve({
			port,
			hostname: "127.0.0.1",
			async fetch(request) {
				if (request.method !== "POST" || new URL(request.url).pathname !== "/command") {
					return new Response("not found", { status: 404 });
				}
				if (core === undefined) {
					return Response.json({ ok: false, error: "native core unavailable" }, { status: 503 });
				}
				try {
					return Response.json(core.request((await request.json()) as AppCommandParams));
				} catch (error) {
					return Response.json({ ok: false, error: String(error) }, { status: 400 });
				}
			},
		});
		console.log(`control server on 127.0.0.1:${port}`);
		writeAutomationEvent("control-server", { port });
	} catch (error) {
		console.error("control server failed to start:", error);
		writeAutomationEvent("control-server-failed", { port, message: String(error) });
	}
}

function openNativeCore(): NativeCore | undefined {
	try {
		const core = NativeCore.open(defaultNativeCoreOptions());
		writeAutomationEvent("native-core-ready");
		return core;
	} catch (error) {
		console.error("failed to open native core:", error);
		writeAutomationEvent("native-core-failed", { message: String(error) });
		return undefined;
	}
}

function writeAutomationEvent(type: string, data: Record<string, unknown> = {}) {
	const reportPath = process.env.TOI_AUTOMATION_REPORT;
	if (!reportPath) {
		return;
	}
	mkdirSync(dirname(reportPath), { recursive: true });
	appendFileSync(reportPath, `${JSON.stringify({ type, time: new Date().toISOString(), ...data })}\n`);
}

async function mainViewUrl(): Promise<string> {
	const channel = await Updater.localInfo.channel();
	if (channel === "dev" && USE_DEV_SERVER) {
		try {
			await fetch(DEV_SERVER_URL, { method: "HEAD" });
			return DEV_SERVER_URL;
		} catch {
			console.log("Vite dev server not running; using bundled UI");
		}
	}
	return "views://mainview/index.html";
}
