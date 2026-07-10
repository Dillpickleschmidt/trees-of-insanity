import { BrowserView, BrowserWindow, Updater, Utils, WGPUView } from "electrobun/bun";
import { appendFileSync, mkdirSync } from "node:fs";
import { dirname } from "node:path";
import type { AppCommandParams, AppCommandResult } from "../shared/shellRpc";
import type {
	ShellRpcSchema,
	UiEventParams,
	ViewportReadyParams,
	ViewportReadyResult,
	ViewportResizeParams,
	ViewportResizeResult,
	ViewportStatus,
} from "../shared/shellRpc";
import { defaultNativeCoreOptions, NativeCore } from "./nativeCore";

const DEV_SERVER_PORT = 5173;
const DEV_SERVER_URL = `http://127.0.0.1:${DEV_SERVER_PORT}`;
const USE_DEV_SERVER = process.env.TOI_USE_VITE_DEV_SERVER === "1";

let mainWindow: BrowserWindow | undefined;
let activeViewportId: number | undefined;
let lastViewportStatus = "";

writeAutomationEvent("main-starting");

mkdirSync(Utils.paths.userData, { recursive: true });
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
			appCommand: (request: AppCommandParams): AppCommandResult => {
				const id = request.id ?? null;
				try {
					return core.request(request);
				} catch (error) {
					writeAutomationEvent("app-command-failed", { method: request.method, message: String(error) });
					return { id, ok: false, error: String(error) };
				}
			},
			viewportReady: ({ id, rect }: ViewportReadyParams): ViewportReadyResult => {
				if (mainWindow === undefined) {
					return { ok: false, error: "main window unavailable" };
				}
				const view = WGPUView.getById(id) ?? WGPUView.adoptExisting(id, {
					windowId: mainWindow.id,
					autoResize: false,
					frame: rect,
				});
				const nativeHandle = view?.getNativeHandle() ?? null;
				const handleResult = {
					ok: view !== undefined && nativeHandle !== null,
					id,
					nativeHandle: nativeHandle === null ? null : String(nativeHandle),
				};
				console.log(
					`native viewport ready id=${id} handle=${handleResult.nativeHandle ?? "null"} rect=${Math.round(rect.width)}x${Math.round(rect.height)}`,
				);
				writeAutomationEvent("viewport-ready", { ...handleResult, rect });

				if (nativeHandle === null) {
					return { ok: false, error: "native viewport handle unavailable" };
				}

				const attach = core.attachViewport(nativeHandle, rect.width, rect.height);
				console.log(
					attach.ok
						? `viewport attached: ${attach.device} ${attach.width}x${attach.height}`
						: `viewport attach failed: ${attach.error}`,
				);
				writeAutomationEvent("viewport-attached", attach);
				if (!attach.ok) {
					return { ok: false, error: attach.error ?? "native viewport attach failed" };
				}
				activeViewportId = id;
				publishViewportStatus(true);
				return { ok: true };
			},
			viewportResize: ({ id, rect }: ViewportResizeParams): ViewportResizeResult => {
				if (id !== activeViewportId) {
					return { ok: false, error: "native viewport is not attached" };
				}
				const width = Math.max(1, Math.trunc(rect.width));
				const height = Math.max(1, Math.trunc(rect.height));
				const resized = core.resizeViewport(width, height);
				const result: ViewportResizeResult = resized.ok
					? { ok: true }
					: { ok: false, error: resized.error ?? "native viewport resize failed" };
				console.log(result.ok ? `viewport resized: ${width}x${height}` : `viewport resize failed: ${result.error}`);
				writeAutomationEvent("viewport-resized", { ...result, width, height, rect });
				return result;
			},
		},
		messages: {
			uiEvent: ({ type, data }: UiEventParams) => {
				writeAutomationEvent(`ui:${type}`, data);
			},
			viewportDetach: ({ id }) => {
				if (id !== activeViewportId) {
					return;
				}
				core.detachViewport();
				activeViewportId = undefined;
				publishViewportStatus(true);
			},
			viewportSurfaceChanged: ({ id }) => {
				if (id === activeViewportId) {
					core.viewportSurfaceChanged();
				}
			},
			viewportCameraInput: ({ id, input }) => {
				if (id === activeViewportId) {
					core.viewportCameraInput(input);
				}
			},
		},
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
mainWindow.on("close", shutdown);

const viewportStatusTimer = setInterval(publishViewportStatus, 250);
viewportStatusTimer.unref();

console.log("Trees of Insanity shell started");
writeAutomationEvent("main-started");

for (const signal of ["SIGINT", "SIGTERM"] as const) {
	process.on(signal, () => {
		shutdown();
		process.exit(0);
	});
}

function publishViewportStatus(force = false) {
	let status: ViewportStatus;
	try {
		status = core.viewportStatus();
	} catch (error) {
		status = {
			...detachedViewportStatus(`Native viewport status unavailable: ${String(error)}`),
			phase: "error",
		};
	}
	const serialized = JSON.stringify(status);
	if (!force && serialized === lastViewportStatus) {
		return;
	}
	lastViewportStatus = serialized;
	rpc.send.viewportStatus(status);
	writeAutomationEvent("viewport-status", status);
}

function detachedViewportStatus(message: string): ViewportStatus {
	return {
		phase: "detached",
		message,
		swapchain: { width: 0, height: 0 },
		color: { width: 0, height: 0 },
		depth: null,
		frame_generation: 0,
	};
}

let shutDown = false;
function shutdown() {
	if (shutDown) {
		return;
	}
	shutDown = true;
	clearInterval(viewportStatusTimer);
	activeViewportId = undefined;
	core.close();
}

function openNativeCore(): NativeCore {
	try {
		const core = NativeCore.open(defaultNativeCoreOptions(Utils.paths.userData));
		writeAutomationEvent("native-core-ready");
		return core;
	} catch (error) {
		console.error("failed to open native core:", error);
		writeAutomationEvent("native-core-failed", { message: String(error) });
		throw error;
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
