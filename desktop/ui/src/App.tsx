import { TriangleAlert } from "lucide-solid";
import { createEffect, createMemo, createSignal, onCleanup, onMount, Show } from "solid-js";

import { PlantDiagnosticLabels } from "~/components/PlantDiagnosticLabels";
import { PlantPanel } from "~/components/PlantPanel";
import { PlantTypesDialog } from "~/components/PlantTypesDialog";
import { Field, PlantTypeOption, PrototypeOption, Readout, Section } from "~/components/panelPrimitives";
import { PrototypeTreeView } from "~/components/PrototypeTree";
import { ResizeHandle } from "~/components/ResizeHandle";
import { TopBar } from "~/components/TopBar";
import { ViewportControls } from "~/components/ViewportControls";
import { Select, SelectContent, SelectItem, SelectTrigger } from "~/components/ui/select";
import { Slider } from "~/components/ui/slider";
import { Viewport } from "~/components/Viewport";
import { appClient, onPlantDiagnosticLabels, onViewportStatus, reportUiEvent } from "~/shell";
import type { ProjectedPlantDiagnosticLabel, ViewportStatus } from "./shared/desktopBridge";
import type {
	AppState,
	GrowthSnapshotSummary,
	PlantDiagnostics,
	PlantState,
	PlantTypeSummary,
	PrototypeSummary,
	PrototypeTree,
	ViewportPreferences,
	ViewportPreferencesView,
	Workspace,
} from "~/types";
import {
	type ColorTheme,
	colorThemes,
	type PlantTypePresetKey,
	plantTypePresetLabel,
	type StatusTone,
	storedTheme,
	type UiTheme,
	uiThemes,
} from "~/uiOptions";

const ageSubmitDelayMs = 33;
const minPanelWidth = 320;
const maxPanelWidth = 720;

const initialState: AppState = {
	active_workspace: "module",
	workspace_previews: [
		{ workspace: "plant", implemented: true },
		{ workspace: "ecosystem", implemented: false },
	],
	prototypes: [],
	active_prototype_id: 0,
	plant_types: [],
	active_plant_type_id: "",
	module_physiological_age: 0,
	fully_grown_age: 0,
	plant_type_parameter_descriptors: [],
};

const initialSummary: GrowthSnapshotSummary = {
	module_physiological_age: 0,
	growth_rate: 0,
	visible_segment_count: 0,
	growing_segment_count: 0,
	mature_segment_count: 0,
	max_diameter: 0,
};

const initialViewportStatus: ViewportStatus = {
	phase: "detached",
	message: "Viewport detached",
	viewport: { width: 0, height: 0 },
	color: { width: 0, height: 0 },
	depth: null,
	frame_generation: 0,
};

function formatNumber(value: number, digits = 3) {
	if (!Number.isFinite(value)) return "—";
	return value.toFixed(digits);
}

export function App() {
	const [state, setState] = createSignal(initialState);
	const [tree, setTree] = createSignal<PrototypeTree | null>(null);
	const [summary, setSummary] = createSignal(initialSummary);
	const [plantState, setPlantState] = createSignal<PlantState>();
	const [viewport, setViewport] = createSignal<ViewportPreferencesView>();
	const [status, setStatus] = createSignal("Starting");
	const [error, setError] = createSignal<string | null>(null);
	const [busy, setBusy] = createSignal(false);
	const [showPlantTypes, setShowPlantTypes] = createSignal(false);
	const [newPlantTypeName, setNewPlantTypeName] = createSignal("");
	const [newPlantTypePresetKey, setNewPlantTypePresetKey] = createSignal<PlantTypePresetKey>("o");
	const [uiTheme, setUiTheme] = createSignal<UiTheme>(storedTheme("toi.uiTheme", "system", uiThemes));
	const [colorTheme, setColorTheme] = createSignal<ColorTheme>(storedTheme("toi.colorTheme", "neutral", colorThemes));
	const [panelWidth, setPanelWidth] = createSignal(384);
	const [dragSliderValue, setDragSliderValue] = createSignal<number | null>(null);
	const [nativeViewportStatus, setNativeViewportStatus] = createSignal(initialViewportStatus);
	const [plantLabels, setPlantLabels] = createSignal<ProjectedPlantDiagnosticLabel[]>([]);
	let disposeViewportStatus = () => {};
	let disposePlantLabels = () => {};
	let latestAgeGeneration = 0;
	let pendingAgeUpdate: { age: number; generation: number } | null = null;
	let ageRequestInFlight = false;
	let ageSubmitTimer: number | undefined;

	const activePrototype = () => state().prototypes.find((prototype) => prototype.id === state().active_prototype_id);
	const activePlantType = () => state().plant_types.find((plantType) => plantType.id === state().active_plant_type_id);
	const sliderValue = () => {
		const dragValue = dragSliderValue();
		if (dragValue !== null) return dragValue;

		const current = state().module_physiological_age;
		const max = state().fully_grown_age;
		if (max <= 0) return 0;
		return Math.round(Math.max(0, Math.min(1, current / max)) * 1000);
	};
	const sliderValues = createMemo<[number]>(() => [sliderValue()]);
	const isMature = () => {
		const max = state().fully_grown_age;
		return max > 0 && state().module_physiological_age >= max - 1e-6;
	};
	const ready = () => state().prototypes.length > 0;

	const statusTone = (): StatusTone => {
		if (error() || status() === "Command failed") return "error";
		if (status() === "Connected") return "live";
		return "idle";
	};

	async function refreshAll() {
		setBusy(true);
		setError(null);
		try {
			const [nextState, nextViewport] = await Promise.all([
				appClient.command("app.get_state"),
				appClient.command("viewport.get_preferences"),
			]);
			setState(nextState);
			setViewport(nextViewport);
			if (nextState.active_workspace === "module") {
				const [nextTree, nextSummary] = await Promise.all([
					appClient.command("module.get_prototype_tree"),
					appClient.command("module.get_growth_snapshot_summary"),
				]);
				setTree(nextTree);
				setSummary(nextSummary);
				setPlantState(undefined);
			} else if (nextState.active_workspace === "plant") {
				setPlantState(await appClient.command("plant.get_state"));
				setTree(null);
			}
			setStatus("Connected");
			reportUiEvent("app-state-loaded", {
				active_workspace: nextState.active_workspace,
				prototypes: nextState.prototypes.length,
				plant_types: nextState.plant_types.length,
			});
		} catch (caught) {
			setError(caught instanceof Error ? caught.message : String(caught));
			setStatus("Command failed");
			reportUiEvent("app-state-failed", { message: error() ?? "unknown" });
		} finally {
			setBusy(false);
		}
	}

	async function runCommand(action: () => Promise<unknown>) {
		setBusy(true);
		setError(null);
		try {
			await action();
			await refreshAll();
		} catch (caught) {
			setError(caught instanceof Error ? caught.message : String(caught));
			setStatus("Command failed");
		} finally {
			setBusy(false);
		}
	}

	function setViewportPreference(patch: Partial<ViewportPreferences>) {
		void runCommand(() => appClient.command("viewport.set_preferences", patch));
	}

	function setPlantTimestep(timestep: number) {
		if (!Number.isFinite(timestep) || timestep <= 0) return;
		void runCommand(() => appClient.command("plant.set_timestep", { timestep }));
	}

	function setPlantDiagnostics(patch: Partial<PlantDiagnostics>) {
		void runCommand(() => appClient.command("plant.set_diagnostics", patch));
	}

	async function createPlantType() {
		setBusy(true);
		setError(null);
		try {
			const plantType = await appClient.command("plant_types.create", {
				name: newPlantTypeName().trim(),
				preset_key: newPlantTypePresetKey(),
			});
			await appClient.command("module.set_active_plant_type", { plant_type_id: plantType.id });
			setNewPlantTypeName("");
			await refreshAll();
		} catch (caught) {
			setError(caught instanceof Error ? caught.message : String(caught));
			setStatus("Command failed");
		} finally {
			setBusy(false);
		}
	}

	function selectNewPlantTypePreset(key: PlantTypePresetKey) {
		setNewPlantTypePresetKey(key);
		if (newPlantTypeName().trim() === "") {
			setNewPlantTypeName(plantTypePresetLabel(key));
		}
	}

	function selectWorkspace(workspace: string) {
		if (workspace === state().active_workspace) return;
		void runCommand(() => appClient.command("workspace.set", { workspace: workspace as Workspace }));
	}

	function moduleAgeFromSliderValue(value: number) {
		const max = state().fully_grown_age;
		return max <= 0 ? 0 : (value / 1000) * max;
	}

	function queueModuleAgeFromSlider(value: number, options: { commit?: boolean; immediate?: boolean } = {}) {
		const age = moduleAgeFromSliderValue(value);
		const generation = ++latestAgeGeneration;
		pendingAgeUpdate = { age, generation };
		setDragSliderValue(options.commit ? null : value);
		setState((current) => ({ ...current, module_physiological_age: age }));
		setError(null);

		if (options.immediate) {
			if (ageSubmitTimer !== undefined) {
				window.clearTimeout(ageSubmitTimer);
				ageSubmitTimer = undefined;
			}
			void flushQueuedModuleAge();
			return;
		}
		scheduleQueuedModuleAgeSubmit(ageSubmitDelayMs);
	}

	function scheduleQueuedModuleAgeSubmit(delayMs: number) {
		if (ageSubmitTimer !== undefined || ageRequestInFlight) return;
		ageSubmitTimer = window.setTimeout(() => {
			ageSubmitTimer = undefined;
			void flushQueuedModuleAge();
		}, delayMs);
	}

	async function flushQueuedModuleAge() {
		if (ageRequestInFlight || pendingAgeUpdate === null) return;

		const update = pendingAgeUpdate;
		pendingAgeUpdate = null;
		ageRequestInFlight = true;
		try {
			await appClient.command("module.set_age", { age: update.age });
			if (update.generation === latestAgeGeneration) {
				const nextSummary = await appClient.command("module.get_growth_snapshot_summary");
				setSummary(nextSummary);
				setStatus("Connected");
			}
		} catch (caught) {
			if (update.generation === latestAgeGeneration) {
				setError(caught instanceof Error ? caught.message : String(caught));
				setStatus("Command failed");
			}
		} finally {
			ageRequestInFlight = false;
			if (pendingAgeUpdate !== null) {
				scheduleQueuedModuleAgeSubmit(ageSubmitDelayMs);
			}
		}
	}

	createEffect(() => {
		document.documentElement.dataset.uiTheme = uiTheme();
		document.documentElement.dataset.colorTheme = colorTheme();
		window.localStorage.setItem("toi.uiTheme", uiTheme());
		window.localStorage.setItem("toi.colorTheme", colorTheme());
	});

	onCleanup(() => {
		disposeViewportStatus();
		disposePlantLabels();
		if (ageSubmitTimer !== undefined) {
			window.clearTimeout(ageSubmitTimer);
		}
	});

	onMount(() => {
		// Surface uncaught webview errors on the automation event channel; a
		// broken Solid render otherwise fails silently (handlers keep working
		// while the UI stops updating).
		window.addEventListener("error", (event) =>
			reportUiEvent("js-error", { message: String(event.error?.stack ?? event.message) }),
		);
		window.addEventListener("unhandledrejection", (event) =>
			reportUiEvent("js-rejection", { message: String((event.reason as Error)?.stack ?? event.reason) }),
		);
		disposeViewportStatus = onViewportStatus(setNativeViewportStatus);
		disposePlantLabels = onPlantDiagnosticLabels(setPlantLabels);
		reportUiEvent("app-mounted");
		void refreshAll();
	});

	return (
		<div class="relative flex h-screen w-screen overflow-hidden bg-transparent text-foreground">
			<div
				class="relative flex h-full shrink-0 flex-col border-r border-border bg-background"
				style={{ width: `${panelWidth()}px` }}
			>
				<TopBar
					status={status()}
					tone={statusTone()}
					busy={busy()}
					previews={state().workspace_previews}
					activeWorkspace={state().active_workspace}
					onSelectWorkspace={selectWorkspace}
					uiTheme={uiTheme()}
					colorTheme={colorTheme()}
					onUiTheme={setUiTheme}
					onColorTheme={setColorTheme}
					onRefresh={() => void refreshAll()}
				/>

				<main class="panel-scroll min-h-0 flex-1 overflow-y-auto px-5 pb-8 pt-5">
					<Show when={error()}>
						<div class="mb-5 flex items-start gap-2.5 rounded-lg border border-destructive/40 bg-destructive/10 px-3.5 py-3 text-[13px] leading-relaxed text-foreground">
							<TriangleAlert class="mt-0.5 h-4 w-4 shrink-0 text-destructive" />
							<span>{error()}</span>
						</div>
					</Show>

					<Show
						when={state().active_workspace === "module"}
						fallback={
							<PlantPanel
								state={plantState()}
								busy={busy()}
								onReset={() => void runCommand(() => appClient.command("plant.reset"))}
								onStep={() => void runCommand(() => appClient.command("plant.step"))}
								onTimestep={setPlantTimestep}
								onDiagnostics={setPlantDiagnostics}
							/>
						}
					>
					<div class="space-y-6">
						{/* SOURCE — what is being grown */}
						<Section eyebrow="Source">
							<Field label="Branch module prototype">
								<Select<PrototypeSummary>
									options={state().prototypes}
									value={activePrototype() ?? null}
									disabled={busy() || state().prototypes.length === 0}
									optionValue="id"
									optionTextValue={(option) => option.name}
									placeholder={state().prototypes.length === 0 ? "No prototypes loaded" : "Select a prototype"}
									onChange={(prototype) => {
										if (!prototype || prototype.id === state().active_prototype_id) return;
										void runCommand(() =>
											appClient.command("module.set_active_prototype", { prototype_id: prototype.id }),
										);
									}}
									itemComponent={(itemProps) => (
										<SelectItem item={itemProps.item}>
											<PrototypeOption prototype={itemProps.item.rawValue} />
										</SelectItem>
									)}
								>
									<SelectTrigger
										placeholder={state().prototypes.length === 0 ? "No prototypes loaded" : "Select a prototype"}
										valueComponent={(option: PrototypeSummary) => <PrototypeOption prototype={option} />}
									/>
									<SelectContent />
								</Select>
							</Field>

							<Field
								label="Plant type"
								action={
									<button
										type="button"
										class="text-[11px] font-medium text-muted-foreground underline-offset-2 transition-colors hover:text-foreground hover:underline focus-visible:outline-none focus-visible:text-foreground"
										onClick={() => setShowPlantTypes(true)}
									>
										Manage
									</button>
								}
							>
								<Select<PlantTypeSummary>
									options={state().plant_types}
									value={activePlantType() ?? null}
									disabled={busy() || state().plant_types.length === 0}
									optionValue="id"
									optionTextValue={(option) => option.name}
									placeholder={state().plant_types.length === 0 ? "No plant types" : "Select a plant type"}
									onChange={(plantType) => {
										if (!plantType || plantType.id === state().active_plant_type_id) return;
										void runCommand(() =>
											appClient.command("module.set_active_plant_type", { plant_type_id: plantType.id }),
										);
									}}
									itemComponent={(itemProps) => (
										<SelectItem item={itemProps.item}>
											<PlantTypeOption plantType={itemProps.item.rawValue} />
										</SelectItem>
									)}
								>
									<SelectTrigger
										placeholder={state().plant_types.length === 0 ? "No plant types" : "Select a plant type"}
										valueComponent={(option: PlantTypeSummary) => <PlantTypeOption plantType={option} />}
									/>
									<SelectContent />
								</Select>
							</Field>
						</Section>

						{/* DEVELOPMENT — the signature growth axis */}
						<Section eyebrow="Development">
							<div class="flex items-end justify-between">
								<div class="flex items-baseline gap-1.5">
									<span class="font-mono text-2xl font-medium tabular-nums tracking-tight">
										{formatNumber(state().module_physiological_age, 2)}
									</span>
									<span class="font-mono text-sm text-muted-foreground tabular-nums">
										/ {formatNumber(state().fully_grown_age, 2)}
									</span>
								</div>
								<Show when={isMature()}>
									<span class="inline-flex items-center gap-1.5 rounded-full bg-grow/15 px-2.5 py-1 text-[11px] font-semibold text-grow">
										<span class="h-1.5 w-1.5 rounded-full bg-grow" />
										Mature
									</span>
								</Show>
							</div>

							<div class="mt-4">
								<Slider
									minValue={0}
									maxValue={1000}
									step={1}
									value={sliderValues()}
									disabled={!ready()}
									onChange={(value) => queueModuleAgeFromSlider(value[0] ?? 0)}
									onChangeEnd={(value) =>
										queueModuleAgeFromSlider(value[0] ?? sliderValue(), { commit: true, immediate: true })
									}
								/>
								<div class="mt-2.5 flex justify-between font-mono text-[11px] uppercase tracking-wider text-muted-foreground">
									<span>Seed</span>
									<span>Mature</span>
								</div>
							</div>

							<p class="mt-3 font-mono text-[11px] text-muted-foreground">
								growth rate&nbsp;&nbsp;
								<span class="text-foreground/80 tabular-nums">{formatNumber(summary().growth_rate, 4)}</span>
							</p>
						</Section>

						{/* GROWTH — live snapshot at the current age */}
						<Section eyebrow="Growth">
							<Readout
								items={[
									{ label: "Visible segments", value: summary().visible_segment_count },
									{ label: "Growing", value: summary().growing_segment_count },
									{ label: "Mature", value: summary().mature_segment_count },
									{ label: "Max diameter", value: formatNumber(summary().max_diameter, 3) },
								]}
							/>
						</Section>

						{/* STRUCTURE — the prototype inspector */}
						<Section eyebrow="Structure">
							<div class="rounded-lg border border-border bg-card/40 p-3.5">
								<PrototypeTreeView tree={tree()} />
							</div>
						</Section>
					</div>
					</Show>
				</main>

				<PlantTypesDialog
					open={showPlantTypes()}
					busy={busy()}
					plantTypes={state().plant_types}
					activePlantTypeId={state().active_plant_type_id}
					parameterDescriptorCount={state().plant_type_parameter_descriptors.length}
					newPlantTypeName={newPlantTypeName()}
					newPlantTypePresetKey={newPlantTypePresetKey()}
					onOpenChange={(open) => setShowPlantTypes(open)}
					onNewPlantTypeName={setNewPlantTypeName}
					onNewPlantTypePreset={selectNewPlantTypePreset}
					onCreatePlantType={() => void createPlantType()}
				/>

				<ResizeHandle
					onResize={(width) => setPanelWidth(Math.min(maxPanelWidth, Math.max(minPanelWidth, width)))}
				/>
			</div>

			<div class="relative min-w-0 flex-1">
				<Viewport />
				<Show when={state().active_workspace === "plant"}>
					<PlantDiagnosticLabels labels={plantLabels()} />
				</Show>
				<ViewportControls
					view={viewport()}
					status={nativeViewportStatus()}
					busy={busy()}
					onChange={setViewportPreference}
				/>
			</div>
		</div>
	);
}
