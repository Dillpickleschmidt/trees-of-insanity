import { For, Show } from "solid-js";

import type { PrototypeTree, PrototypeTreeItem } from "~/types";

function TreeItem(props: { item: PrototypeTreeItem; depth: number }) {
	const isNode = () => props.item.kind === "node";
	return (
		<li class="relative">
			<div class="flex items-center gap-2 py-[3px] font-mono text-[12.5px] leading-tight">
				<span
					class="inline-flex h-3.5 w-3.5 shrink-0 items-center justify-center text-[10px]"
					classList={{ "text-grow": isNode(), "text-muted-foreground": !isNode() }}
					aria-hidden="true"
				>
					{isNode() ? "●" : "│"}
				</span>
				<span classList={{ "text-foreground": isNode(), "text-muted-foreground": !isNode() }}>
					{props.item.label}
				</span>
			</div>
			<Show when={props.item.children.length > 0}>
				<ul class="ml-[7px] border-l border-border pl-3">
					<For each={props.item.children}>{(child) => <TreeItem item={child} depth={props.depth + 1} />}</For>
				</ul>
			</Show>
		</li>
	);
}

export function PrototypeTreeView(props: { tree: PrototypeTree | null }) {
	return (
		<Show
			when={props.tree}
			fallback={<p class="font-mono text-[12.5px] text-muted-foreground">No prototype loaded.</p>}
		>
			{(tree) => (
				<ul>
					<TreeItem item={tree().root} depth={0} />
				</ul>
			)}
		</Show>
	);
}
