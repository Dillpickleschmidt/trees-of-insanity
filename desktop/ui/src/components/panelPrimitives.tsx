import { For, type JSX } from "solid-js";

import type { PlantTypeSummary, PrototypeSummary } from "~/shared/desktopActions";

export function Section(props: { eyebrow: string; children: JSX.Element }) {
	return (
		<section class="rise-in space-y-3.5">
			<div class="eyebrow">{props.eyebrow}</div>
			{props.children}
		</section>
	);
}

export function Field(props: { label: string; action?: JSX.Element; children: JSX.Element }) {
	return (
		<div class="space-y-1.5">
			<div class="flex items-center justify-between">
				<label class="text-[12px] font-medium text-muted-foreground">{props.label}</label>
				{props.action}
			</div>
			{props.children}
		</div>
	);
}

export function PrototypeOption(props: { prototype: PrototypeSummary }) {
	return (
		<div class="min-w-0">
			<div class="truncate text-sm font-medium text-foreground">{props.prototype.name}</div>
			<div class="truncate font-mono text-[11px] text-muted-foreground tabular-nums">
				{props.prototype.node_count} nodes · {props.prototype.segment_count} segments
			</div>
		</div>
	);
}

export function PlantTypeOption(props: { plantType: PlantTypeSummary }) {
	return <div class="truncate text-sm font-medium text-foreground">{props.plantType.name}</div>;
}

export function Readout(props: { items: { label: string; value: string | number }[] }) {
	return (
		<dl class="grid grid-cols-2 gap-x-6">
			<For each={props.items}>
				{(item) => (
					<div class="flex items-baseline justify-between gap-2 border-b border-border/60 py-1.5">
						<dt class="truncate text-[12px] text-muted-foreground">{item.label}</dt>
						<dd class="shrink-0 font-mono text-[13px] tabular-nums">{item.value}</dd>
					</div>
				)}
			</For>
		</dl>
	);
}
