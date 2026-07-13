import type { CommandRequest, CommandResponse } from "./shared/desktopActions";
import type {
	ProjectedPlantDiagnosticLabel,
	Rect,
	ViewportCameraInput,
	ViewportStatus,
} from "./shared/desktopBridge";
import { createAppClient } from "./appClient";

type QtSignal<T extends unknown[]> = {
	connect(listener: (...args: T) => void): void;
	disconnect(listener: (...args: T) => void): void;
};

type DesktopBridge = {
	bootstrap(callback: (response: string) => void): void;
	dispatch(action: string, callback: (response: string) => void): void;
	uiEvent(type: string, data: string): void;
	setViewportRect(x: number, y: number, width: number, height: number, devicePixelRatio: number): void;
	cameraInput(kind: string, dx: number, dy: number, viewportHeight: number): void;
	viewportStatusChanged: QtSignal<[string]>;
	plantDiagnosticLabelsChanged: QtSignal<[string]>;
};

declare global {
	interface Window {
		qt?: { webChannelTransport: unknown };
		QWebChannel?: new (
			transport: unknown,
			ready: (channel: { objects: { desktopBridge: DesktopBridge } }) => void,
		) => unknown;
	}
}

const bridgePromise = new Promise<DesktopBridge>((resolve, reject) => {
	if (window.qt === undefined || window.QWebChannel === undefined) {
		reject(new Error("Qt WebChannel transport unavailable"));
		return;
	}
	new window.QWebChannel(window.qt.webChannelTransport, (channel) => resolve(channel.objects.desktopBridge));
});

export const appClient = createAppClient(async (request: CommandRequest): Promise<CommandResponse> => {
	const bridge = await bridgePromise;
	return new Promise((resolve, reject) => {
		const invoke = request.method === "app.get_state"
			? (callback: (response: string) => void) => bridge.bootstrap(callback)
			: (callback: (response: string) => void) => bridge.dispatch(JSON.stringify(request), callback);
		invoke((response) => {
			try {
				resolve(JSON.parse(response) as CommandResponse);
			} catch (cause) {
				reject(cause);
			}
		});
	});
});

export function reportUiEvent(type: string, data: Record<string, unknown> = {}) {
	void bridgePromise.then((bridge) => bridge.uiEvent(type, JSON.stringify(data)));
}

export function updateViewportRect(rect: Rect) {
	void bridgePromise.then((bridge) =>
		bridge.setViewportRect(rect.x, rect.y, rect.width, rect.height, window.devicePixelRatio),
	);
}

export function sendViewportCameraInput(input: ViewportCameraInput) {
	void bridgePromise.then((bridge) => bridge.cameraInput(input.kind, input.dx, input.dy, input.viewportHeight));
}

export function onPlantDiagnosticLabels(listener: (labels: ProjectedPlantDiagnosticLabel[]) => void) {
	let bridge: DesktopBridge | undefined;
	const receive = (serialized: string) => listener(JSON.parse(serialized) as ProjectedPlantDiagnosticLabel[]);
	void bridgePromise.then((resolved) => {
		bridge = resolved;
		bridge.plantDiagnosticLabelsChanged.connect(receive);
	});
	return () => bridge?.plantDiagnosticLabelsChanged.disconnect(receive);
}

export function onViewportStatus(listener: (status: ViewportStatus) => void) {
	let bridge: DesktopBridge | undefined;
	const receive = (serialized: string) => listener(JSON.parse(serialized) as ViewportStatus);
	void bridgePromise.then((resolved) => {
		bridge = resolved;
		bridge.viewportStatusChanged.connect(receive);
	});
	return () => bridge?.viewportStatusChanged.disconnect(receive);
}
