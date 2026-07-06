import { Select as KobalteSelect } from "@kobalte/core/select";
import { Check, ChevronDown } from "lucide-solid";
import type { Component, ComponentProps, JSX } from "solid-js";
import { splitProps } from "solid-js";
import { cn } from "~/lib/utils";

const Select = KobalteSelect;

const SelectTrigger: Component<
	ComponentProps<typeof KobalteSelect.Trigger> & {
		placeholder?: string;
		// biome-ignore lint/suspicious/noExplicitAny: selected option type is generic across usages.
		valueComponent?: (option: any) => JSX.Element;
	}
> = (props) => {
	const [local, rest] = splitProps(props, ["class", "placeholder", "valueComponent"]);
	return (
		<KobalteSelect.Trigger
			class={cn(
				"flex min-h-10 w-full items-center justify-between gap-2 rounded-lg border border-border bg-card px-3 py-2 text-left",
				"text-sm ring-offset-background transition-colors hover:border-input",
				"focus:outline-none focus-visible:ring-2 focus-visible:ring-ring focus-visible:ring-offset-2",
				"disabled:cursor-not-allowed disabled:opacity-50 [&>span]:line-clamp-1",
				local.class,
			)}
			{...rest}
		>
			<KobalteSelect.Value<unknown> class="min-w-0 flex-1">
				{(state) =>
					state.selectedOption() ? (
						(local.valueComponent?.(state.selectedOption()) ?? (state.selectedOption() as string))
					) : (
						<span class="text-muted-foreground">{local.placeholder ?? "Select…"}</span>
					)
				}
			</KobalteSelect.Value>
			<KobalteSelect.Icon>
				<ChevronDown class="h-4 w-4 shrink-0 opacity-40" />
			</KobalteSelect.Icon>
		</KobalteSelect.Trigger>
	);
};

const SelectContent: Component<ComponentProps<typeof KobalteSelect.Content>> = (props) => {
	const [local, rest] = splitProps(props, ["class"]);
	return (
		<KobalteSelect.Portal>
			<KobalteSelect.Content
				class={cn(
					"relative z-50 max-h-96 min-w-[var(--kb-popper-anchor-width)] overflow-hidden rounded-lg border border-border bg-popover text-popover-foreground shadow-xl shadow-black/30",
					"data-[expanded]:animate-in data-[closed]:animate-out",
					"data-[closed]:fade-out-0 data-[expanded]:fade-in-0",
					"data-[closed]:zoom-out-95 data-[expanded]:zoom-in-95",
					local.class,
				)}
				{...rest}
			>
				<KobalteSelect.Listbox class="max-h-80 overflow-y-auto p-1.5" />
			</KobalteSelect.Content>
		</KobalteSelect.Portal>
	);
};

const SelectItem: Component<ComponentProps<typeof KobalteSelect.Item>> = (props) => {
	const [local, rest] = splitProps(props, ["class", "children"]);
	return (
		<KobalteSelect.Item
			class={cn(
				"relative flex w-full cursor-default select-none items-center rounded-md py-2 pl-8 pr-2 text-sm outline-none",
				"transition-colors focus:bg-accent focus:text-accent-foreground",
				"data-[disabled]:pointer-events-none data-[disabled]:opacity-50",
				local.class,
			)}
			{...rest}
		>
			<span class="absolute left-2.5 flex h-3.5 w-3.5 items-center justify-center text-grow">
				<KobalteSelect.ItemIndicator>
					<Check class="h-4 w-4" />
				</KobalteSelect.ItemIndicator>
			</span>
			<KobalteSelect.ItemLabel class="min-w-0 flex-1">{local.children}</KobalteSelect.ItemLabel>
		</KobalteSelect.Item>
	);
};

const SelectLabel: Component<ComponentProps<typeof KobalteSelect.Label>> = (props) => {
	const [local, rest] = splitProps(props, ["class"]);
	return <KobalteSelect.Label class={cn("py-1.5 pl-8 pr-2 text-sm font-semibold", local.class)} {...rest} />;
};

const SelectGroup = KobalteSelect.Section;

export { Select, SelectTrigger, SelectContent, SelectItem, SelectLabel, SelectGroup };
