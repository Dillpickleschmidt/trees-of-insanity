import { Electroview } from "electrobun/view";
import { createSignal, onCleanup, onMount } from "solid-js";
import type { Rect, ShellRpcSchema } from "../shared/shellRpc";

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

export function App() {
	const [viewportStatus, setViewportStatus] = createSignal("waiting for native viewport");
	let viewportElement: WgpuTagElement | undefined;

	onMount(() => {
		let attached = false;
		let disposed = false;
		const handleReady = (event: CustomEvent<{ id: number }>) => {
			void notifyViewportReady(event.detail.id);
		};

		reportUiEvent("app-mounted");
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
