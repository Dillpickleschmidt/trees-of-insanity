import { For, Show } from "solid-js";

import { Field } from "~/components/panelPrimitives";
import { Button } from "~/components/ui/button";
import { Dialog, DialogContent, DialogDescription, DialogHeader, DialogTitle } from "~/components/ui/dialog";
import { Select, SelectContent, SelectItem, SelectTrigger } from "~/components/ui/select";
import type { PlantTypeSummary } from "~/types";
import { type PlantTypePresetKey, plantTypePresetKeys, plantTypePresetLabel } from "~/uiOptions";

export function PlantTypesDialog(props: {
	open: boolean;
	busy: boolean;
	plantTypes: PlantTypeSummary[];
	activePlantTypeId: string;
	newPlantTypeName: string;
	newPlantTypePresetKey: PlantTypePresetKey;
	onOpenChange: (open: boolean) => void;
	onNewPlantTypeName: (name: string) => void;
	onNewPlantTypePreset: (key: PlantTypePresetKey) => void;
	onCreatePlantType: () => void;
}) {
	return (
		<Dialog open={props.open} onOpenChange={props.onOpenChange}>
			<DialogContent>
				<DialogHeader>
					<DialogTitle>Plant types</DialogTitle>
					<DialogDescription>The plant types available to this workspace.</DialogDescription>
				</DialogHeader>

				<div class="rounded-lg border border-border bg-card/60 p-3.5">
					<div class="eyebrow mb-3">Create</div>
					<div class="space-y-3">
						<Field label="Base preset">
							<Select<PlantTypePresetKey>
								options={plantTypePresetKeys}
								value={props.newPlantTypePresetKey}
								disabled={props.busy}
								onChange={(key) => key && props.onNewPlantTypePreset(key)}
								itemComponent={(itemProps) => (
									<SelectItem item={itemProps.item}>{plantTypePresetLabel(itemProps.item.rawValue)}</SelectItem>
								)}
							>
								<SelectTrigger valueComponent={(key: PlantTypePresetKey) => plantTypePresetLabel(key)} />
								<SelectContent />
							</Select>
						</Field>
						<Field label="Name">
							<input
								type="text"
								value={props.newPlantTypeName}
								placeholder={plantTypePresetLabel(props.newPlantTypePresetKey)}
								disabled={props.busy}
								class="flex h-10 w-full rounded-lg border border-border bg-background px-3 py-2 text-sm text-foreground outline-none transition-colors placeholder:text-muted-foreground focus-visible:ring-2 focus-visible:ring-ring disabled:cursor-not-allowed disabled:opacity-50"
								onInput={(event) => props.onNewPlantTypeName(event.currentTarget.value)}
							/>
						</Field>
						<Button class="w-full" disabled={props.busy} onClick={() => void props.onCreatePlantType()}>
							Create plant type
						</Button>
					</div>
				</div>

				<Show
					when={props.plantTypes.length > 0}
					fallback={<p class="py-6 text-center text-sm text-muted-foreground">No plant types yet.</p>}
				>
					<ul class="space-y-1.5 py-1">
						<For each={props.plantTypes}>
							{(plantType) => (
								<li
									class="flex items-center justify-between rounded-lg border border-border bg-card px-3.5 py-3"
									classList={{ "ring-1 ring-grow/50": plantType.id === props.activePlantTypeId }}
								>
									<div class="min-w-0">
										<div class="truncate text-sm font-medium">{plantType.name}</div>
										<div class="truncate font-mono text-[11px] text-muted-foreground">{plantType.id}</div>
									</div>
									<Show when={plantType.id === props.activePlantTypeId}>
										<span class="shrink-0 text-[11px] font-semibold text-grow">Active</span>
									</Show>
								</li>
							)}
						</For>
					</ul>
				</Show>
			</DialogContent>
		</Dialog>
	);
}
