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

export type PlantTypeParameterDescriptor = {
	key: keyof PlantTypeParameters;
	min: number | null;
	max: number | null;
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
	plant_type_parameter_descriptors: PlantTypeParameterDescriptor[];
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

export type SegmentState = "growing" | "mature";

export type Vec3Tuple = [number, number, number];

export type GrowthSnapshotSegment = {
	source_segment_id: number;
	parent_position: Vec3Tuple;
	child_position: Vec3Tuple;
	diameter: number;
	state: SegmentState;
};

export type GrowthSnapshot = {
	module_physiological_age: number;
	growth_rate: number;
	segments: GrowthSnapshotSegment[];
};

export type PlantState = {
	plant_age: number;
	root_physiological_age: number;
	root_fully_grown_age: number;
	timestep: number;
	paused: boolean;
	root_prototype_id: number;
	plant_type_id: string;
	module_diagnostic_labels_visible: boolean;
	direct_light_bounding_spheres_visible: boolean;
	direct_light_exposure: number;
	accumulated_light: number;
	vigor: number;
	growth_rate: number;
};

export type PlantDiagnostics = Pick<
	PlantState,
	"module_diagnostic_labels_visible" | "direct_light_bounding_spheres_visible"
>;

export type PlantTypeParameters = {
	plant_max_age: number;
	root_max_vigor: number;
	plant_growth_rate: number;
	apical_control: number;
	mature_apical_control: number | null;
	determinacy: number;
	mature_determinacy: number | null;
	flowering_age: number;
	tropism_angle: number;
	tropism_weight: number;
	tropism_strength: number;
	terminal_thickness: number;
	length_growth_scale: number;
};

export type PlantType = {
	id: string;
	name: string;
	parameters: PlantTypeParameters;
};

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
	"project.save": { params: NoParams; result: NoParams };
	"module.list_prototypes": { params: NoParams; result: PrototypeSummary[] };
	"module.set_active_prototype": { params: { prototype_id: number }; result: NoParams };
	"module.set_active_plant_type": { params: { plant_type_id: string }; result: NoParams };
	"module.set_age": { params: { age: number }; result: NoParams };
	"module.get_prototype_tree": { params: NoParams; result: PrototypeTree };
	"module.get_growth_snapshot_summary": { params: NoParams; result: GrowthSnapshotSummary };
	"module.get_growth_snapshot": { params: NoParams; result: GrowthSnapshot };
	"plant.get_state": { params: NoParams; result: PlantState };
	"plant.reset": { params: NoParams; result: NoParams };
	"plant.step": { params: NoParams; result: NoParams };
	"plant.set_timestep": { params: { timestep: number }; result: NoParams };
	"plant.set_diagnostics": { params: Partial<PlantDiagnostics>; result: NoParams };
	"workspace.set": { params: { workspace: Workspace }; result: NoParams };
	"plant_types.list": { params: NoParams; result: PlantTypeSummary[] };
	"plant_types.get": { params: { plant_type_id: string }; result: PlantType };
	"plant_types.create": { params: { name: string; preset_key?: PlantTypePresetKey }; result: PlantType };
	"plant_types.delete": { params: { plant_type_id: string }; result: NoParams };
	"plant_types.update": {
		params: { plant_type_id: string; name?: string; parameters?: Partial<PlantTypeParameters> };
		result: NoParams;
	};
	"viewport.get_preferences": { params: NoParams; result: ViewportPreferencesView };
	"viewport.set_preferences": { params: Partial<ViewportPreferences>; result: NoParams };
};

export type CommandMethod = keyof CommandMap;
export type CommandParams<M extends CommandMethod> = CommandMap[M]["params"];
export type CommandResult<M extends CommandMethod> = CommandMap[M]["result"];

// params is optional exactly when every field of the params type is optional.
export type CommandParamsArg<M extends CommandMethod> = {} extends CommandParams<M>
	? [params?: CommandParams<M>]
	: [params: CommandParams<M>];

export type CommandId = number | string | null;

export type CommandRequest<M extends CommandMethod = CommandMethod> = {
	id?: CommandId;
	method: M;
	params?: CommandParams<M>;
};

export type CommandResponse<M extends CommandMethod = CommandMethod> =
	| { id: CommandId; ok: true; result: CommandResult<M> }
	| { id: CommandId; ok: false; error: string; code?: string };
