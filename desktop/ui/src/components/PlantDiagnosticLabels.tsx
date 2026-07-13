import { For, Show } from "solid-js";

import type { ProjectedPlantDiagnosticLabel } from "../shared/desktopBridge";

function format(value: number) {
	return Number.isFinite(value) ? value.toFixed(3) : "—";
}

export function PlantDiagnosticLabels(props: { labels: ProjectedPlantDiagnosticLabel[] }) {
	return (
		<For each={props.labels}>
			{(label) => (
				<Show when={label.visible}>
					<div
						class="pointer-events-none absolute z-20 min-w-44 -translate-x-1/2 -translate-y-full rounded-md border border-white/15 bg-background/85 px-3 py-2 font-mono text-[11px] leading-5 shadow-lg"
						style={{
							left: `${label.x * 100}%`,
							top: `calc(${label.y * 100}% - 10px)`,
						}}
					>
						<div>Direct light exposure&nbsp; {format(label.direct_light_exposure)}</div>
						<div>Accumulated light&nbsp;&nbsp;&nbsp;&nbsp; {format(label.accumulated_light)}</div>
						<div>Vigor&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp; {format(label.vigor)}</div>
					</div>
				</Show>
			)}
		</For>
	);
}
