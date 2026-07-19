import { Play, RotateCcw, Square } from "lucide-solid";
import { Show } from "solid-js";

import { Readout, Section } from "~/components/panelPrimitives";
import { Switch, SwitchControl, SwitchThumb } from "~/components/ui/switch";
import type { PlantDiagnostics, PlantState } from "~/shared/desktopActions";

function formatNumber(value: number, digits = 3) {
	return Number.isFinite(value) ? value.toFixed(digits) : "—";
}

function formatInputNumber(value: number) {
	return Number.isFinite(value) ? String(Number(value.toPrecision(8))) : "";
}

export function PlantPanel(props: {
	state: PlantState | undefined;
	busy: boolean;
	running: boolean;
	onReset: () => void;
	onRun: () => void;
	onStop: () => void;
	onRunSettings: (patch: { target_age?: number; step_size?: number }) => void;
	onDiagnostics: (patch: Partial<PlantDiagnostics>) => void;
}) {
	return (
		<Show
			when={props.state}
			fallback={<div class="h-40 animate-pulse rounded-lg border border-border bg-card/40" />}
		>
			{(state) => (
				<div class="space-y-6">
					<Section eyebrow="Simulation">
						<div class="grid grid-cols-2 gap-2">
							<button type="button" disabled={props.busy || props.running} onClick={props.onReset}
								class="inline-flex h-9 items-center justify-center gap-2 rounded-md border border-border bg-card px-3 text-sm font-medium transition-colors hover:bg-accent disabled:opacity-40">
								<RotateCcw class="size-4" /> Reset
							</button>
							<button
								type="button"
								disabled={props.busy}
								onClick={() => (props.running ? props.onStop() : props.onRun())}
								class="inline-flex h-9 items-center justify-center gap-2 rounded-md bg-primary px-3 text-sm font-medium text-primary-foreground transition-opacity hover:opacity-90 disabled:opacity-40"
							>
								<Show when={props.running} fallback={<><Play class="size-4" /> Run</>}>
									<Square class="size-3.5" /> Stop
								</Show>
							</button>
						</div>
						<div class="mt-4 grid grid-cols-2 gap-2">
							<label class="block text-[12px] text-muted-foreground">
								Target age
								<input
									type="number"
									min="0"
									step="1"
									value={formatInputNumber(state().target_age)}
									disabled={props.busy || props.running}
									onChange={(event) => props.onRunSettings({ target_age: event.currentTarget.valueAsNumber })}
									class="mt-1.5 h-9 w-full rounded-md border border-border bg-background px-3 font-mono text-sm text-foreground focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-ring disabled:opacity-40"
								/>
							</label>
							<label class="block text-[12px] text-muted-foreground">
								Step size (years)
								<input
									type="number"
									min="0.001"
									step="0.1"
									value={formatInputNumber(state().step_size)}
									disabled={props.busy || props.running}
									onChange={(event) => props.onRunSettings({ step_size: event.currentTarget.valueAsNumber })}
									class="mt-1.5 h-9 w-full rounded-md border border-border bg-background px-3 font-mono text-sm text-foreground focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-ring disabled:opacity-40"
								/>
							</label>
						</div>
					</Section>

					<Section eyebrow="Plant state">
						<Readout items={[
							{ label: "Plant age", value: formatNumber(state().plant_age, 2) },
							{ label: "Root age", value: formatNumber(state().root_physiological_age, 2) },
							{ label: "Mature age", value: formatNumber(state().root_fully_grown_age, 2) },
							{ label: "Growth rate", value: formatNumber(state().growth_rate, 4) },
						]} />
					</Section>

					<Section eyebrow="Diagnostics">
						<div class="space-y-4">
							<Switch checked={state().module_diagnostic_labels_visible} disabled={props.busy || props.running}
								onChange={(value) => props.onDiagnostics({ module_diagnostic_labels_visible: value })}
								class="flex items-center justify-between gap-4">
								<span class="text-[13px]">Module diagnostic labels</span>
								<SwitchControl><SwitchThumb /></SwitchControl>
							</Switch>
							<Switch checked={state().direct_light_bounding_spheres_visible} disabled={props.busy || props.running}
								onChange={(value) => props.onDiagnostics({ direct_light_bounding_spheres_visible: value })}
								class="flex items-center justify-between gap-4">
								<span class="text-[13px]">Direct-light bounding spheres</span>
								<SwitchControl><SwitchThumb /></SwitchControl>
							</Switch>
							{/* Both diagnostics tint the same module surface, so only one can show. */}
							<Switch checked={state().module_accumulated_light_visible} disabled={props.busy || props.running}
								onChange={(value) =>
									props.onDiagnostics({
										module_accumulated_light_visible: value,
										...(value ? { module_vigor_visible: false } : {}),
									})
								}
								class="flex items-center justify-between gap-4">
								<span class="text-[13px]">Module accumulated light</span>
								<SwitchControl><SwitchThumb /></SwitchControl>
							</Switch>
							<Switch checked={state().module_vigor_visible} disabled={props.busy || props.running}
								onChange={(value) =>
									props.onDiagnostics({
										module_vigor_visible: value,
										...(value ? { module_accumulated_light_visible: false } : {}),
									})
								}
								class="flex items-center justify-between gap-4">
								<span class="text-[13px]">Module vigor</span>
								<SwitchControl><SwitchThumb /></SwitchControl>
							</Switch>
							<Switch checked={state().mature_terminal_markers_visible} disabled={props.busy || props.running}
								onChange={(value) => props.onDiagnostics({ mature_terminal_markers_visible: value })}
								class="flex items-center justify-between gap-4">
								<span class="text-[13px]">Mature-terminal markers</span>
								<SwitchControl><SwitchThumb /></SwitchControl>
							</Switch>
						</div>
					</Section>
				</div>
			)}
		</Show>
	);
}
