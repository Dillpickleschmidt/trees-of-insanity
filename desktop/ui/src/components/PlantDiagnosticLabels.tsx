import { createEffect, createMemo, createSignal, For, Show } from "solid-js";

import type { ProjectedPlantDiagnosticLabel } from "../shared/desktopBridge";

export function PlantDiagnosticLabels(props: { labels: ProjectedPlantDiagnosticLabel[] }) {
	const [moduleStackOrder, setModuleStackOrder] = createSignal(props.labels.map((label) => label.module_id));

	createEffect(() => {
		const currentModuleIds = props.labels.map((label) => label.module_id);
		const currentModuleIdSet = new Set(currentModuleIds);
		setModuleStackOrder((previous) => {
			const retained = previous.filter((moduleId) => currentModuleIdSet.has(moduleId));
			const retainedSet = new Set(retained);
			const next = [...retained, ...currentModuleIds.filter((moduleId) => !retainedSet.has(moduleId))];
			return next.length === previous.length && next.every((moduleId, index) => moduleId === previous[index])
				? previous
				: next;
		});
	});

	const moduleStackRanks = createMemo(
		() => new Map(moduleStackOrder().map((moduleId, index) => [moduleId, index])),
	);

	const bringModuleLabelToFront = (moduleId: number) => {
		setModuleStackOrder((previous) =>
			previous[previous.length - 1] === moduleId
				? previous
				: [...previous.filter((existingId) => existingId !== moduleId), moduleId],
		);
	};

	return (
		<div class="pointer-events-none absolute inset-0 z-20">
			<svg aria-hidden="true" class="absolute size-0">
				<defs>
					<filter
						id="module-diagnostic-label-outline"
						x="-20%"
						y="-20%"
						width="140%"
						height="160%"
						color-interpolation-filters="sRGB"
					>
						<feMorphology in="SourceAlpha" operator="dilate" radius="1" result="expanded" />
						<feComposite
							in="expanded"
							in2="SourceAlpha"
							operator="arithmetic"
							k2="1"
							k3="-1"
							result="outline"
						/>
						<feFlood flood-color="#000000" flood-opacity="0.55" result="outline-color" />
						<feComposite in="outline-color" in2="outline" operator="in" result="colored-outline" />
						<feMerge>
							<feMergeNode in="colored-outline" />
							<feMergeNode in="SourceGraphic" />
						</feMerge>
					</filter>
				</defs>
			</svg>
			<For each={props.labels}>
				{(label) => (
					<Show when={label.visible}>
						<div
							class="module-diagnostic-label-shell pointer-events-none absolute -translate-x-1/2 -translate-y-full"
							style={{
								left: `${label.x * 100}%`,
								top: `${label.y * 100}%`,
								"z-index": moduleStackRanks().get(label.module_id) ?? 0,
							}}
						>
							<div
								class="module-diagnostic-label grid grid-cols-[auto_auto] gap-x-3 whitespace-nowrap bg-background/85 px-2 pt-1 font-mono text-[11px] leading-4"
								onPointerEnter={() => bringModuleLabelToFront(label.module_id)}
							>
								<span>Direct light</span>
								<span class="justify-self-end text-right tabular-nums">{format(label.direct_light_exposure)}</span>
								<span>Accum. light</span>
								<span class="justify-self-end text-right tabular-nums">{format(label.accumulated_light)}</span>
								<span>Vigor</span>
								<span class="justify-self-end text-right tabular-nums">{format(label.vigor)}</span>
							</div>
						</div>
					</Show>
				)}
			</For>
		</div>
	);
}

function format(value: number) {
	return Number.isFinite(value) ? value.toFixed(3) : "—";
}
