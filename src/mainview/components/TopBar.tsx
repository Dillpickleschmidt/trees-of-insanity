import { createMemo, For } from "solid-js";
import { RefreshCw, Settings2, Sprout } from "lucide-solid";

import { Field } from "~/components/panelPrimitives";
import { Popover, PopoverContent, PopoverTrigger } from "~/components/ui/popover";
import { Select, SelectContent, SelectItem, SelectTrigger } from "~/components/ui/select";
import {
	type ColorTheme,
	colorThemeLabels,
	colorThemes,
	type StatusTone,
	type UiTheme,
	uiThemeLabels,
	uiThemes,
} from "~/uiOptions";

export function TopBar(props: {
	status: string;
	tone: StatusTone;
	busy: boolean;
	previews: { workspace: string; implemented: boolean }[];
	uiTheme: UiTheme;
	colorTheme: ColorTheme;
	onUiTheme: (value: UiTheme) => void;
	onColorTheme: (value: ColorTheme) => void;
	onRefresh: () => void;
}) {
	const tabs = createMemo(() => [{ workspace: "module", implemented: true }, ...props.previews]);
	const dotClass = () =>
		props.tone === "live" ? "bg-grow" : props.tone === "error" ? "bg-destructive" : "bg-muted-foreground";
	return (
		<header class="flex items-center gap-3 border-b border-border px-4 py-2">
			{/* Brand + live status — app identity, kept compact on the left */}
			<div class="flex min-w-0 items-center gap-2">
				<Sprout class="h-[18px] w-[18px] shrink-0 text-grow" />
				<span class="truncate text-[13px] font-semibold tracking-tight">Trees of Insanity</span>
				<span
					class="h-1.5 w-1.5 shrink-0 rounded-full"
					classList={{ [dotClass()]: true, "status-pulse": props.tone !== "live" }}
					title={props.status}
				/>
			</div>

			<nav class="ml-auto flex items-center gap-0.5">
				<For each={tabs()}>
					{(tab) => (
						<button
							type="button"
							disabled={!tab.implemented}
							title={tab.implemented ? `${tab.workspace} workspace` : `${tab.workspace} workspace — placeholder`}
							class="rounded-md px-2 py-1 text-[12px] font-medium capitalize transition-colors focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-ring disabled:cursor-not-allowed"
							classList={{
								"bg-secondary text-foreground": tab.workspace === "module",
								"text-muted-foreground hover:text-foreground": tab.implemented && tab.workspace !== "module",
								"text-muted-foreground/40": !tab.implemented,
							}}
						>
							{tab.workspace}
						</button>
					)}
				</For>
			</nav>

			<Popover gutter={8} placement="bottom-end">
				<PopoverTrigger
					class="inline-flex h-8 w-8 shrink-0 items-center justify-center rounded-lg text-muted-foreground transition-colors hover:bg-accent hover:text-foreground focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-ring data-[expanded]:bg-accent data-[expanded]:text-foreground"
					aria-label="Settings"
				>
					<Settings2 class="h-4 w-4" />
				</PopoverTrigger>
				<PopoverContent class="w-60" showClose={false}>
					<div class="eyebrow mb-3">Appearance</div>
					<div class="space-y-3">
						<Field label="Mode">
							<Select<UiTheme>
								options={uiThemes}
								value={props.uiTheme}
								onChange={(value) => value && props.onUiTheme(value)}
								itemComponent={(itemProps) => (
									<SelectItem item={itemProps.item}>{uiThemeLabels[itemProps.item.rawValue]}</SelectItem>
								)}
							>
								<SelectTrigger valueComponent={(value: UiTheme) => uiThemeLabels[value]} />
								<SelectContent />
							</Select>
						</Field>
						<Field label="Color">
							<Select<ColorTheme>
								options={colorThemes}
								value={props.colorTheme}
								onChange={(value) => value && props.onColorTheme(value)}
								itemComponent={(itemProps) => (
									<SelectItem item={itemProps.item}>{colorThemeLabels[itemProps.item.rawValue]}</SelectItem>
								)}
							>
								<SelectTrigger valueComponent={(value: ColorTheme) => colorThemeLabels[value]} />
								<SelectContent />
							</Select>
						</Field>
					</div>
					<div class="mt-3 border-t border-border pt-3">
						<button
							type="button"
							onClick={props.onRefresh}
							disabled={props.busy}
							class="flex w-full items-center justify-between rounded-md px-2 py-1.5 text-[13px] text-muted-foreground transition-colors hover:bg-accent hover:text-foreground focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-ring disabled:opacity-50"
						>
							<span>Refresh data</span>
							<RefreshCw class="h-3.5 w-3.5" classList={{ "animate-spin": props.busy }} />
						</button>
					</div>
				</PopoverContent>
			</Popover>
		</header>
	);
}
