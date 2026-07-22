// Typed action catalog shared by the Solid UI and Qt WebChannel adapter.
// Keep these shapes synchronized with desktop/src/shell/desktop_actions.cpp.

export type WorkspacePreview = {
	workspace: string;
	implemented: boolean;
};

export type PrototypeSummary = {
	id: number;
	name: string;
	node_count: number;
	segment_count: number;
};

export type PlantTypeSummary = {
	id: string;
	name: string;
};

export type AppState = {
	active_workspace: string;
	workspace_previews: WorkspacePreview[];
	prototypes: PrototypeSummary[];
	active_prototype_id: number;
	plant_types: PlantTypeSummary[];
	active_plant_type_id: string;
	module_physiological_age: number;
	fully_grown_age: number;
};

export type Workspace = "module" | "plant" | "ecosystem";

export type PrototypeTreeItemKind = "node" | "segment";

export type PrototypeTreeItem = {
	kind: PrototypeTreeItemKind;
	id: number;
	label: string;
	children: PrototypeTreeItem[];
};

export type PrototypeTree = {
	root: PrototypeTreeItem;
};

export type GrowthSnapshotSummary = {
	module_physiological_age: number;
	growth_rate: number;
	visible_segment_count: number;
	growing_segment_count: number;
	mature_segment_count: number;
	max_diameter: number;
};

export type PlantRootState = {
	physiological_age: number;
	fully_grown_age: number;
	direct_light_exposure: number;
	accumulated_light: number;
	vigor: number;
	growth_rate: number;
};

export type PlantState = {
	plant_age: number;
	root: PlantRootState | null;
	target_age: number;
	step_size: number;
	root_prototype_id: number;
	plant_type_id: string;
	module_diagnostic_labels_visible: boolean;
	direct_light_bounding_spheres_visible: boolean;
	module_accumulated_light_visible: boolean;
	module_vigor_visible: boolean;
	mature_terminal_markers_visible: boolean;
};

export type PlantDiagnostics = Pick<
	PlantState,
	| "module_diagnostic_labels_visible"
	| "direct_light_bounding_spheres_visible"
	| "module_accumulated_light_visible"
	| "module_vigor_visible"
	| "mature_terminal_markers_visible"
>;

export type HdriEnvironment = {
	id: string;
	name: string;
	bundled: boolean;
};

export type ViewportPreferences = {
	guides_visible: boolean;
	world_origin_axes_visible: boolean;
	hdri_backdrop_visible: boolean;
	active_hdri_environment_id: string;
};

export type ViewportPreferencesView = {
	preferences: ViewportPreferences;
	hdri_environments: HdriEnvironment[];
};

// preset keys 'a'..'p' map to the built-in plant type presets.
export type PlantTypePresetKey =
	| "a" | "b" | "c" | "d" | "e" | "f" | "g" | "h"
	| "i" | "j" | "k" | "l" | "m" | "n" | "o" | "p";

type NoParams = Record<string, never>;

export type CommandMap = {
	"app.get_state": { params: NoParams; result: AppState };
	"module.set_active_prototype": { params: { prototype_id: number }; result: NoParams };
	"module.set_active_plant_type": { params: { plant_type_id: string }; result: NoParams };
	"module.set_age": { params: { age: number }; result: NoParams };
	"module.get_prototype_tree": { params: NoParams; result: PrototypeTree };
	"module.get_growth_snapshot_summary": { params: NoParams; result: GrowthSnapshotSummary };
	"plant.get_state": { params: NoParams; result: PlantState };
	"plant.reset": { params: NoParams; result: NoParams };
	"plant.run": { params: NoParams; result: NoParams };
	"plant.stop": { params: NoParams; result: NoParams };
	"plant.set_run_settings": {
		params: { target_age?: number; step_size?: number };
		result: NoParams;
	};
	"plant.set_diagnostics": { params: Partial<PlantDiagnostics>; result: NoParams };
	"workspace.set": { params: { workspace: Workspace }; result: NoParams };
	"plant_types.create": { params: { name: string; preset_key?: PlantTypePresetKey }; result: PlantTypeSummary };
	"viewport.get_preferences": { params: NoParams; result: ViewportPreferencesView };
	"viewport.set_frames_per_second": { params: { frames_per_second: number }; result: NoParams };
	"viewport.set_preferences": { params: Partial<ViewportPreferences>; result: NoParams };
};

export type CommandMethod = keyof CommandMap;
export type CommandParams<M extends CommandMethod> = CommandMap[M]["params"];
export type CommandResult<M extends CommandMethod> = CommandMap[M]["result"];

// params is optional exactly when every field of the params type is optional.
export type CommandParamsArg<M extends CommandMethod> = {} extends CommandParams<M>
	? [params?: CommandParams<M>]
	: [params: CommandParams<M>];

export type CommandRequest<M extends CommandMethod = CommandMethod> = {
	method: M;
	params?: CommandParams<M>;
};

export type CommandResponse<M extends CommandMethod = CommandMethod> =
	| { ok: true; result: CommandResult<M> }
	| { ok: false; error: string; code?: string };
