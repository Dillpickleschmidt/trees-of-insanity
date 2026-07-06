import { Electroview } from "electrobun/view";
import { createSignal, onCleanup, onMount, Show } from "solid-js";
import type { AppState } from "../shared/appCommands";
import type { Rect, ShellRpcSchema } from "../shared/shellRpc";
import { createAppClient } from "./appClient";

const rpc = Electroview.defineRPC<ShellRpcSchema>({
	maxRequestTime: 10_000,
	handlers: {
		requests: {},
		messages: {},
	},
});

const electrobun = new Electroview({ rpc });
void electrobun;
void reportUiEvent("module-loaded");

const appClient = createAppClient((request) => rpc.request.appCommand(request));

export function App() {
	const [viewportStatus, setViewportStatus] = createSignal("waiting for native viewport");
	const [appState, setAppState] = createSignal<AppState>();
	const [stateError, setStateError] = createSignal<string>();
	let viewportElement: WgpuTagElement | undefined;

	onMount(() => {
		let attached = false;
		let disposed = false;
		const handleReady = (event: CustomEvent<{ id: number }>) => {
			void notifyViewportReady(event.detail.id);
		};

		reportUiEvent("app-mounted");
		void loadAppState();
		void attachViewportReadyListener();

		onCleanup(() => {
			disposed = true;
			if (attached) {
				viewportElement?.off("ready", handleReady);
			}
		});

		async function attachViewportReadyListener() {
			await customElements.whenDefined("electrobun-wgpu");
			reportUiEvent("wgpu-defined", { hasElement: viewportElement !== undefined });
			if (disposed || viewportElement === undefined) {
				return;
			}
			viewportElement.on("ready", handleReady);
			attached = true;
			reportUiEvent("wgpu-listener-attached", { wgpuViewId: viewportElement.wgpuViewId });
			if (viewportElement.wgpuViewId !== null) {
				void notifyViewportReady(viewportElement.wgpuViewId);
			}
		}
	});

	async function loadAppState() {
		try {
			const state = await appClient.command("app.get_state");
			setAppState(state);
			reportUiEvent("app-state-loaded", {
				active_workspace: state.active_workspace,
				prototypes: state.prototypes.length,
				plant_types: state.plant_types.length,
			});
		} catch (error) {
			setStateError(String(error));
			reportUiEvent("app-state-failed", { message: String(error) });
		}
	}

	async function notifyViewportReady(id: number) {
		reportUiEvent("viewport-ready-callback", { id });
		if (viewportElement === undefined) {
			return;
		}
		const rect = viewportRect(viewportElement);
		setViewportStatus(`native view ${id} attached`);
		try {
			const result = await rpc.request.viewportReady({ id, rect });
			setViewportStatus(result.ok ? `native handle ${result.nativeHandle}` : "native handle unavailable");
		} catch (error) {
			console.error("viewportReady failed", error);
			setViewportStatus("native viewport attach failed");
		}
	}

	return (
		<div class="grid min-h-screen grid-cols-[360px_minmax(0,1fr)] bg-zinc-950 text-zinc-100">
			<aside class="border-r border-zinc-800 bg-zinc-950/95 p-6">
				<p class="text-xs uppercase tracking-[0.28em] text-emerald-300">Trees of Insanity</p>
				<h1 class="mt-4 text-2xl font-semibold tracking-tight">Growth lab</h1>
				<p class="mt-3 text-sm leading-6 text-zinc-400">
					Solid/Tailwind controls on the left. Native GPU viewport on the right.
				</p>

				<section class="mt-8 rounded-2xl border border-zinc-800 bg-zinc-900/60 p-4">
					<h2 class="text-sm font-medium text-zinc-200">Viewport</h2>
					<p class="mt-2 text-sm text-zinc-400">{viewportStatus()}</p>
				</section>

				<section class="mt-4 rounded-2xl border border-zinc-800 bg-zinc-900/60 p-4">
					<h2 class="text-sm font-medium text-zinc-200">Native core</h2>
					<Show
						when={appState()}
						fallback={<p class="mt-2 text-sm text-zinc-400">{stateError() ?? "loading application state…"}</p>}
					>
						{(state) => (
							<dl class="mt-2 space-y-1 text-sm text-zinc-400">
								<div class="flex justify-between">
									<dt>Workspace</dt>
									<dd class="text-zinc-200">{state().active_workspace}</dd>
								</div>
								<div class="flex justify-between">
									<dt>Prototypes</dt>
									<dd class="text-zinc-200">{state().prototypes.length}</dd>
								</div>
								<div class="flex justify-between">
									<dt>Plant types</dt>
									<dd class="text-zinc-200">{state().plant_types.length}</dd>
								</div>
							</dl>
						)}
					</Show>
				</section>
			</aside>

			<main class="min-w-0 bg-black p-3">
				<div class="h-full overflow-hidden rounded-2xl border border-zinc-800 bg-black shadow-2xl shadow-emerald-950/20">
					<electrobun-wgpu
						id="native-viewport"
						class="h-full w-full"
						ref={(element) => {
							viewportElement = element as WgpuTagElement;
						}}
					/>
				</div>
			</main>
		</div>
	);
}

function reportUiEvent(type: string, data?: Record<string, unknown>) {
	void rpc.request.uiEvent({ type, data }).catch(() => {});
}

function viewportRect(element: HTMLElement): Rect {
	const rect = element.getBoundingClientRect();
	return {
		x: rect.x,
		y: rect.y,
		width: rect.width,
		height: rect.height,
	};
}
