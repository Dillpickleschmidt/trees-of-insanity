import { TriangleAlert } from "lucide-solid";
import { createEffect, createMemo, createResource, createSignal, onCleanup, onMount, Show } from "solid-js";

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
	PlantDiagnostics,
	PlantTypeSummary,
	PrototypeSummary,
	ViewportPreferences,
	Workspace,
} from "~/types";
import {
	type ColorTheme,
	colorThemes,
	type PlantTypePresetKey,
	plantTypePresetLabel,
	storedTheme,
	type UiTheme,
	uiThemes,
} from "~/uiOptions";

const ageSubmitDelayMs = 33;
const minPanelWidth = 320;
const maxPanelWidth = 720;

const initialViewportStatus: ViewportStatus = {
	phase: "detached",
	message: "Viewport detached",
	viewport: { width: 0, height: 0 },
	color: { width: 0, height: 0 },
	depth: null,
	frame_generation: 0,
	scene_frame_count: 0,
	precomposition_count: 0,
};

function formatNumber(value: number, digits = 3) {
	if (!Number.isFinite(value)) return "—";
	return value.toFixed(digits);
}

export function App() {
	const [error, setError] = createSignal<string | null>(null);
	const [workspacePending, setWorkspacePending] = createSignal(false);
	const [modulePending, setModulePending] = createSignal(false);
	const [plantPending, setPlantPending] = createSignal(false);
	const [viewportPending, setViewportPending] = createSignal(false);
	const [showPlantTypes, setShowPlantTypes] = createSignal(false);
	const [newPlantTypeName, setNewPlantTypeName] = createSignal("");
	const [newPlantTypePresetKey, setNewPlantTypePresetKey] = createSignal<PlantTypePresetKey>("o");
	const [uiTheme, setUiTheme] = createSignal<UiTheme>(storedTheme("toi.uiTheme", "system", uiThemes));
	const [colorTheme, setColorTheme] = createSignal<ColorTheme>(storedTheme("toi.colorTheme", "neutral", colorThemes));
	const [panelWidth, setPanelWidth] = createSignal(384);
	const [dragSliderValue, setDragSliderValue] = createSignal<number | null>(null);
	const [nativeViewportStatus, setNativeViewportStatus] = createSignal(initialViewportStatus);
	const [plantLabels, setPlantLabels] = createSignal<ProjectedPlantDiagnosticLabel[]>([]);
	const [stateResource, { mutate: mutateState, refetch: refetchState }] = createResource(async () => {
		const current = await appClient.command("app.get_state");
		reportUiEvent("app-state-loaded", {
			active_workspace: current.active_workspace,
			prototypes: current.prototypes.length,
			plant_types: current.plant_types.length,
		});
		return current;
	});
	const state = () => (stateResource.state === "errored" ? undefined : stateResource());
	const [treeResource, { refetch: refetchTree }] = createResource(
		() => (state()?.active_workspace === "module" ? "module" : undefined),
		() => appClient.command("module.get_prototype_tree"),
	);
	const tree = () => (treeResource.state === "errored" ? undefined : treeResource());
	const [summaryResource, { mutate: mutateSummary, refetch: refetchSummary }] = createResource(
		() => (state()?.active_workspace === "module" ? "module" : undefined),
		() => appClient.command("module.get_growth_snapshot_summary"),
	);
	const summary = () => (summaryResource.state === "errored" ? undefined : summaryResource());
	const [plantStateResource, { refetch: refetchPlantState }] = createResource(
		() => (state()?.active_workspace === "plant" ? "plant" : undefined),
		() => appClient.command("plant.get_state"),
	);
	const plantState = () => (plantStateResource.state === "errored" ? undefined : plantStateResource());
	const [viewportResource, { refetch: refetchViewport }] = createResource(() =>
		appClient.command("viewport.get_preferences"),
	);
	const viewport = () => (viewportResource.state === "errored" ? undefined : viewportResource());
	let disposeViewportStatus = () => {};
	let disposePlantLabels = () => {};
	let latestAgeGeneration = 0;
	let pendingAgeUpdate: { age: number; generation: number } | null = null;
	let ageRequestInFlight = false;
	let ageSubmitTimer: number | undefined;

	const activePrototype = () => {
		const current = state();
		return current?.prototypes.find((prototype) => prototype.id === current.active_prototype_id);
	};
	const activePlantType = () => {
		const current = state();
		return current?.plant_types.find((plantType) => plantType.id === current.active_plant_type_id);
	};
	const sliderValue = () => {
		const dragValue = dragSliderValue();
		if (dragValue !== null) return dragValue;

		const current = state();
		if (current === undefined || current.fully_grown_age <= 0) return 0;
		return Math.round(
			Math.max(0, Math.min(1, current.module_physiological_age / current.fully_grown_age)) * 1000,
		);
	};
	const sliderValues = createMemo<[number]>(() => [sliderValue()]);
	const isMature = () => {
		const current = state();
		return (
			current !== undefined &&
			current.fully_grown_age > 0 &&
			current.module_physiological_age >= current.fully_grown_age - 1e-6
		);
	};
	const moduleBusy = () =>
		workspacePending() || modulePending() || treeResource.loading || summaryResource.loading;
	const plantBusy = () => workspacePending() || plantPending() || plantStateResource.loading;
	const viewportBusy = () => workspacePending() || viewportPending() || viewportResource.loading;
	const resourceError = () =>
		stateResource.error ??
		viewportResource.error ??
		(state()?.active_workspace === "module" ? treeResource.error ?? summaryResource.error : undefined) ??
		(state()?.active_workspace === "plant" ? plantStateResource.error : undefined);
	const errorMessage = () => {
		const caught = error() ?? resourceError();
		return caught === undefined ? null : formatError(caught);
	};

	function handleCommandError(caught: unknown) {
		setError(formatError(caught));
	}

	function refreshData() {
		setError(null);
		void refetchState();
		void refetchViewport();
		if (state()?.active_workspace === "module") {
			void refetchTree();
			void refetchSummary();
		} else if (state()?.active_workspace === "plant") {
			void refetchPlantState();
		}
	}

	async function runModuleCommand(action: () => Promise<unknown>) {
		setModulePending(true);
		setError(null);
		try {
			await action();
			await Promise.all([refetchState(), refetchTree(), refetchSummary()]);
		} catch (caught) {
			handleCommandError(caught);
		} finally {
			setModulePending(false);
		}
	}

	async function runPlantCommand(action: () => Promise<unknown>) {
		setPlantPending(true);
		setError(null);
		try {
			await action();
			await refetchPlantState();
		} catch (caught) {
			handleCommandError(caught);
		} finally {
			setPlantPending(false);
		}
	}

	async function runViewportCommand(patch: Partial<ViewportPreferences>) {
		setViewportPending(true);
		setError(null);
		try {
			await appClient.command("viewport.set_preferences", patch);
			await refetchViewport();
		} catch (caught) {
			handleCommandError(caught);
		} finally {
			setViewportPending(false);
		}
	}

	function setViewportPreference(patch: Partial<ViewportPreferences>) {
		void runViewportCommand(patch);
	}

	function setPlantTimestep(timestep: number) {
		if (!Number.isFinite(timestep) || timestep <= 0) return;
		void runPlantCommand(() => appClient.command("plant.set_timestep", { timestep }));
	}

	function setPlantDiagnostics(patch: Partial<PlantDiagnostics>) {
		void runPlantCommand(() => appClient.command("plant.set_diagnostics", patch));
	}

	async function createPlantType() {
		await runModuleCommand(async () => {
			const plantType = await appClient.command("plant_types.create", {
				name: newPlantTypeName().trim(),
				preset_key: newPlantTypePresetKey(),
			});
			await appClient.command("module.set_active_plant_type", { plant_type_id: plantType.id });
			setNewPlantTypeName("");
		});
	}

	function selectNewPlantTypePreset(key: PlantTypePresetKey) {
		setNewPlantTypePresetKey(key);
		if (newPlantTypeName().trim() === "") {
			setNewPlantTypeName(plantTypePresetLabel(key));
		}
	}

	async function selectWorkspace(workspace: string) {
		if (workspace === state()?.active_workspace) return;
		setWorkspacePending(true);
		setError(null);
		try {
			await appClient.command("workspace.set", { workspace: workspace as Workspace });
			await Promise.all([refetchState(), refetchViewport()]);
		} catch (caught) {
			handleCommandError(caught);
		} finally {
			setWorkspacePending(false);
		}
	}

	function moduleAgeFromSliderValue(value: number) {
		const current = state();
		if (current === undefined || current.fully_grown_age <= 0) return 0;
		return (value / 1000) * current.fully_grown_age;
	}

	function queueModuleAgeFromSlider(value: number, options: { commit?: boolean; immediate?: boolean } = {}) {
		const age = moduleAgeFromSliderValue(value);
		const generation = ++latestAgeGeneration;
		pendingAgeUpdate = { age, generation };
		setDragSliderValue(options.commit ? null : value);
		mutateState((current) =>
			current === undefined ? current : { ...current, module_physiological_age: age },
		);
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
				if (update.generation === latestAgeGeneration) {
					mutateSummary(nextSummary);
				}
			}
		} catch (caught) {
			if (update.generation === latestAgeGeneration) {
				handleCommandError(caught);
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
	});

	return (
		<div class="relative flex h-screen w-screen overflow-hidden bg-transparent text-foreground">
			<div
				class="relative flex h-full shrink-0 flex-col border-r border-border bg-background"
				style={{ width: `${panelWidth()}px` }}
			>
				<Show
					when={state()}
					fallback={
						<Show
							when={stateResource.error}
							fallback={<div class="m-5 h-40 animate-pulse rounded-lg border border-border bg-card/40" />}
						>
							<div class="m-5 space-y-3 text-sm text-destructive">
								<p>{formatError(stateResource.error)}</p>
								<button type="button" class="underline" onClick={() => void refetchState()}>
									Retry
								</button>
							</div>
						</Show>
					}
				>
					<TopBar
						status={errorMessage() ? "Command failed" : "Connected"}
						tone={errorMessage() ? "error" : "live"}
						busy={workspacePending() || stateResource.loading}
						previews={state()!.workspace_previews}
						activeWorkspace={state()!.active_workspace}
						onSelectWorkspace={(workspace) => void selectWorkspace(workspace)}
						uiTheme={uiTheme()}
						colorTheme={colorTheme()}
						onUiTheme={setUiTheme}
						onColorTheme={setColorTheme}
						onRefresh={refreshData}
					/>

					<main class="panel-scroll min-h-0 flex-1 overflow-y-auto px-5 pb-8 pt-5">
						<Show when={errorMessage()}>
							<div class="mb-5 flex items-start gap-2.5 rounded-lg border border-destructive/40 bg-destructive/10 px-3.5 py-3 text-[13px] leading-relaxed text-foreground">
								<TriangleAlert class="mt-0.5 h-4 w-4 shrink-0 text-destructive" />
								<span>{errorMessage()}</span>
							</div>
						</Show>

						<Show
							when={state()!.active_workspace === "module"}
							fallback={
								<PlantPanel
									state={plantState()}
									busy={plantBusy()}
									onReset={() =>
										void runPlantCommand(() => appClient.command("plant.reset"))
									}
									onStep={() => void runPlantCommand(() => appClient.command("plant.step"))}
									onTimestep={setPlantTimestep}
									onDiagnostics={setPlantDiagnostics}
								/>
							}
						>
							<div class="space-y-6">
								<Section eyebrow="Source">
									<Field label="Branch module prototype">
										<Select<PrototypeSummary>
											options={state()!.prototypes}
											value={activePrototype() ?? null}
											disabled={moduleBusy() || state()!.prototypes.length === 0}
											optionValue="id"
											optionTextValue={(option) => option.name}
											placeholder={
												state()!.prototypes.length === 0
													? "No prototypes loaded"
													: "Select a prototype"
											}
											onChange={(prototype) => {
												if (!prototype || prototype.id === state()!.active_prototype_id) return;
												void runModuleCommand(() =>
													appClient.command("module.set_active_prototype", {
														prototype_id: prototype.id,
													}),
												);
											}}
											itemComponent={(itemProps) => (
												<SelectItem item={itemProps.item}>
													<PrototypeOption prototype={itemProps.item.rawValue} />
												</SelectItem>
											)}
										>
											<SelectTrigger
												placeholder={
													state()!.prototypes.length === 0
														? "No prototypes loaded"
														: "Select a prototype"
												}
												valueComponent={(option: PrototypeSummary) => (
													<PrototypeOption prototype={option} />
												)}
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
											options={state()!.plant_types}
											value={activePlantType() ?? null}
											disabled={moduleBusy() || state()!.plant_types.length === 0}
											optionValue="id"
											optionTextValue={(option) => option.name}
											placeholder={
												state()!.plant_types.length === 0 ? "No plant types" : "Select a plant type"
											}
											onChange={(plantType) => {
												if (!plantType || plantType.id === state()!.active_plant_type_id) return;
												void runModuleCommand(() =>
													appClient.command("module.set_active_plant_type", {
														plant_type_id: plantType.id,
													}),
												);
											}}
											itemComponent={(itemProps) => (
												<SelectItem item={itemProps.item}>
													<PlantTypeOption plantType={itemProps.item.rawValue} />
												</SelectItem>
											)}
										>
											<SelectTrigger
												placeholder={
													state()!.plant_types.length === 0
														? "No plant types"
														: "Select a plant type"
												}
												valueComponent={(option: PlantTypeSummary) => (
													<PlantTypeOption plantType={option} />
												)}
											/>
											<SelectContent />
										</Select>
									</Field>
								</Section>

								<Show
									when={summary()}
									fallback={<div class="h-52 animate-pulse rounded-lg border border-border bg-card/40" />}
								>
									{(growthSummary) => (
										<>
											<Section eyebrow="Development">
												<div class="flex items-end justify-between">
													<div class="flex items-baseline gap-1.5">
														<span class="font-mono text-2xl font-medium tabular-nums tracking-tight">
															{formatNumber(state()!.module_physiological_age, 2)}
														</span>
														<span class="font-mono text-sm text-muted-foreground tabular-nums">
															/ {formatNumber(state()!.fully_grown_age, 2)}
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
														disabled={moduleBusy() || state()!.prototypes.length === 0}
														onChange={(value) => queueModuleAgeFromSlider(value[0] ?? 0)}
														onChangeEnd={(value) =>
															queueModuleAgeFromSlider(value[0] ?? sliderValue(), {
																commit: true,
																immediate: true,
															})
														}
													/>
													<div class="mt-2.5 flex justify-between font-mono text-[11px] uppercase tracking-wider text-muted-foreground">
														<span>Seed</span>
														<span>Mature</span>
													</div>
												</div>

												<p class="mt-3 font-mono text-[11px] text-muted-foreground">
													growth rate&nbsp;&nbsp;
													<span class="text-foreground/80 tabular-nums">
														{formatNumber(growthSummary().growth_rate, 4)}
													</span>
												</p>
											</Section>

											<Section eyebrow="Growth">
												<Readout
													items={[
														{ label: "Visible segments", value: growthSummary().visible_segment_count },
														{ label: "Growing", value: growthSummary().growing_segment_count },
														{ label: "Mature", value: growthSummary().mature_segment_count },
														{
															label: "Max diameter",
															value: formatNumber(growthSummary().max_diameter, 3),
														},
													]}
												/>
											</Section>
										</>
									)}
								</Show>

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
						busy={moduleBusy()}
						plantTypes={state()!.plant_types}
						activePlantTypeId={state()!.active_plant_type_id}
						newPlantTypeName={newPlantTypeName()}
						newPlantTypePresetKey={newPlantTypePresetKey()}
						onOpenChange={setShowPlantTypes}
						onNewPlantTypeName={setNewPlantTypeName}
						onNewPlantTypePreset={selectNewPlantTypePreset}
						onCreatePlantType={() => void createPlantType()}
					/>
				</Show>

				<ResizeHandle
					onResize={(width) => setPanelWidth(Math.min(maxPanelWidth, Math.max(minPanelWidth, width)))}
				/>
			</div>

			<div class="relative min-w-0 flex-1">
				<Viewport />
				<Show when={state()?.active_workspace === "plant"}>
					<PlantDiagnosticLabels labels={plantLabels()} />
				</Show>
				<ViewportControls
					view={viewport()}
					status={nativeViewportStatus()}
					busy={viewportBusy()}
					onChange={setViewportPreference}
				/>
			</div>
		</div>
	);
}

function formatError(caught: unknown) {
	return caught instanceof Error ? caught.message : String(caught);
}
