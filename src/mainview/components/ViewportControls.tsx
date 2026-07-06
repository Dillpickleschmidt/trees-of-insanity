import { Field, Section } from "~/components/panelPrimitives";
import { Select, SelectContent, SelectItem, SelectTrigger } from "~/components/ui/select";
import { Switch, SwitchControl, SwitchThumb } from "~/components/ui/switch";
import type { HdriEnvironment, ViewportPreferences, ViewportPreferencesView } from "~/types";

export function ViewportControls(props: {
	view: ViewportPreferencesView;
	busy: boolean;
	onChange: (patch: Partial<ViewportPreferences>) => void;
}) {
	const activeEnvironment = () =>
		props.view.hdri_environments.find((environment) => environment.id === props.view.preferences.active_hdri_environment_id) ??
		null;

	return (
		<Section eyebrow="Viewport">
			<Field label="Render mode">
				<div class="inline-flex items-center gap-1.5 rounded-md bg-secondary px-2.5 py-1 text-[12px] font-medium">
					<span class="h-1.5 w-1.5 rounded-full bg-grow" />
					RTX
				</div>
			</Field>

			<Field label="HDRI environment">
				<Select<HdriEnvironment>
					options={props.view.hdri_environments}
					value={activeEnvironment()}
					disabled={props.busy || props.view.hdri_environments.length === 0}
					optionValue="id"
					optionTextValue={(environment) => environment.name}
					placeholder="No environments"
					onChange={(environment) => {
						if (environment && environment.id !== props.view.preferences.active_hdri_environment_id) {
							props.onChange({ active_hdri_environment_id: environment.id });
						}
					}}
					itemComponent={(itemProps) => <SelectItem item={itemProps.item}>{itemProps.item.rawValue.name}</SelectItem>}
				>
					<SelectTrigger placeholder="No environments" valueComponent={(environment: HdriEnvironment) => environment.name} />
					<SelectContent />
				</Select>
			</Field>

			<div class="space-y-2.5">
				<Toggle
					label="HDRI backdrop"
					checked={props.view.preferences.hdri_backdrop_visible}
					disabled={props.busy}
					onChange={(value) => props.onChange({ hdri_backdrop_visible: value })}
				/>
				<Toggle
					label="Guides"
					checked={props.view.preferences.guides_visible}
					disabled={props.busy}
					onChange={(value) => props.onChange({ guides_visible: value })}
				/>
				<Toggle
					label="World-origin axes"
					checked={props.view.preferences.world_origin_axes_visible}
					disabled={props.busy}
					onChange={(value) => props.onChange({ world_origin_axes_visible: value })}
				/>
			</div>
		</Section>
	);
}

function Toggle(props: { label: string; checked: boolean; disabled?: boolean; onChange: (value: boolean) => void }) {
	return (
		<Switch checked={props.checked} onChange={props.onChange} disabled={props.disabled} class="flex items-center justify-between">
			<span class="text-[13px] text-foreground">{props.label}</span>
			<SwitchControl>
				<SwitchThumb />
			</SwitchControl>
		</Switch>
	);
}
