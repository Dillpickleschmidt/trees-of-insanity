import { Box, ChevronDown, Grid3x3, Sparkles } from "lucide-solid";
import { Show } from "solid-js";

import { Field } from "~/components/panelPrimitives";
import { Popover, PopoverContent, PopoverTrigger } from "~/components/ui/popover";
import { Select, SelectContent, SelectItem, SelectTrigger } from "~/components/ui/select";
import { Switch, SwitchControl, SwitchThumb } from "~/components/ui/switch";
import type { HdriEnvironment, ViewportPreferences, ViewportPreferencesView } from "~/types";
import type { ViewportStatus } from "../shared/desktopBridge";

export function ViewportControls(props: {
	view: ViewportPreferencesView | undefined;
	status: ViewportStatus;
	busy: boolean;
	onChange: (patch: Partial<ViewportPreferences>) => void;
}) {
	const preferences = () => props.view!.preferences;
	const activeEnvironment = () =>
		props.view!.hdri_environments.find(
			(environment) => environment.id === preferences().active_hdri_environment_id,
		) ?? null;

	return (
		<>
			<Show when={props.status.phase !== "ready"}>
				<div
					class="pointer-events-none absolute left-4 top-4 z-30 max-w-72 rounded-md border border-white/15 bg-background/70 px-3 py-2 text-xs shadow-lg"
					classList={{ "text-destructive": props.status.phase === "error" }}
				>
					<div class="font-medium capitalize">{props.status.phase}</div>
					<div class="truncate text-muted-foreground">{props.status.message}</div>
				</div>
			</Show>

			<Show
				when={props.view}
				fallback={<div class="absolute right-4 top-4 z-30 h-10 w-48 animate-pulse rounded-md bg-background/60" />}
			>
				<div class="absolute right-4 top-4 z-30 flex items-center rounded-md border border-white/10 bg-background/60 px-1 shadow-lg">
					<button
						type="button"
						title="Toggle viewport guides"
						aria-label="Toggle viewport guides"
						aria-pressed={preferences().guides_visible}
						disabled={props.busy}
						onClick={() => props.onChange({ guides_visible: !preferences().guides_visible })}
						class="flex size-8 items-center justify-center text-foreground/40 transition-colors hover:text-foreground focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-inset focus-visible:ring-ring disabled:opacity-40"
						classList={{ "text-foreground": preferences().guides_visible }}
					>
						<Grid3x3 class="size-4" />
					</button>

					<Popover gutter={8} placement="bottom-end">
						<PopoverTrigger
							class="flex h-8 w-5 items-center justify-center text-foreground/45 transition-colors hover:text-foreground focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-inset focus-visible:ring-ring data-[expanded]:text-foreground disabled:opacity-40"
							aria-label="Guide settings"
							title="Guide settings"
							disabled={props.busy}
						>
							<ChevronDown class="size-3" />
						</PopoverTrigger>
						<PopoverContent class="w-52" showClose={false}>
							<div class="eyebrow mb-3">Guides</div>
							<Switch
								checked={preferences().world_origin_axes_visible}
								disabled={props.busy}
								onChange={(value) => props.onChange({ world_origin_axes_visible: value })}
								class="flex items-center justify-between gap-4"
							>
								<span class="text-[13px] text-foreground">World-origin axes</span>
								<SwitchControl>
									<SwitchThumb />
								</SwitchControl>
							</Switch>
						</PopoverContent>
					</Popover>

					<div class="mx-1 h-5 w-px bg-white/15" />

					<button
						type="button"
						title="Workbench rendering is not available"
						aria-label="Workbench rendering unavailable"
						disabled
						class="flex size-8 items-center justify-center text-foreground/25"
					>
						<Box class="size-4" />
					</button>
					<button
						type="button"
						title="ovrtx rendered mode"
						aria-label="ovrtx rendered mode"
						aria-pressed="true"
						class="flex size-8 items-center justify-center text-foreground transition-colors focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-inset focus-visible:ring-ring"
					>
						<Sparkles class="size-4" />
					</button>

					<Popover gutter={8} placement="bottom-end">
						<PopoverTrigger
							class="flex h-8 w-5 items-center justify-center text-foreground/45 transition-colors hover:text-foreground focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-inset focus-visible:ring-ring data-[expanded]:text-foreground disabled:opacity-40"
							aria-label="Render settings"
							title="Render settings"
							disabled={props.busy}
						>
							<ChevronDown class="size-3" />
						</PopoverTrigger>
						<PopoverContent class="w-60" showClose={false}>
							<div class="eyebrow mb-3">Render settings</div>
							<div class="space-y-4">
								<Switch
									checked={preferences().hdri_backdrop_visible}
									disabled={props.busy}
									onChange={(value) => props.onChange({ hdri_backdrop_visible: value })}
									class="flex items-center justify-between gap-4"
								>
									<span class="text-[13px] text-foreground">HDRI background</span>
									<SwitchControl>
										<SwitchThumb />
									</SwitchControl>
								</Switch>

								<Field label="Environment">
									<Select<HdriEnvironment>
										options={props.view!.hdri_environments}
										value={activeEnvironment()}
										disabled={props.busy || props.view!.hdri_environments.length === 0}
										optionValue="id"
										optionTextValue={(environment) => environment.name}
										placeholder="No environments"
										onChange={(environment) => {
											if (environment && environment.id !== preferences().active_hdri_environment_id) {
												props.onChange({ active_hdri_environment_id: environment.id });
											}
										}}
										itemComponent={(itemProps) => (
											<SelectItem item={itemProps.item}>{itemProps.item.rawValue.name}</SelectItem>
										)}
									>
										<SelectTrigger
											placeholder="No environments"
											valueComponent={(environment: HdriEnvironment) => environment.name}
										/>
										<SelectContent />
									</Select>
								</Field>
							</div>
						</PopoverContent>
					</Popover>
				</div>
			</Show>
		</>
	);
}
