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
						class="pointer-events-none absolute z-20 grid -translate-x-1/2 -translate-y-full grid-cols-[auto_auto] gap-x-3 whitespace-nowrap rounded-md border border-white/15 bg-background/85 px-2 py-1 font-mono text-[11px] leading-4 shadow-lg"
						style={{
							left: `${label.x * 100}%`,
							top: `calc(${label.y * 100}% - 10px)`,
						}}
					>
						<span>Direct light</span>
						<span class="justify-self-end text-right tabular-nums">{format(label.direct_light_exposure)}</span>
						<span>Accum. light</span>
						<span class="justify-self-end text-right tabular-nums">{format(label.accumulated_light)}</span>
						<span>Vigor</span>
						<span class="justify-self-end text-right tabular-nums">{format(label.vigor)}</span>
					</div>
				</Show>
			)}
		</For>
	);
}
