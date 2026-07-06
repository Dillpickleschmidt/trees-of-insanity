// Typed catalog of the native application-command seam.
//
// These shapes mirror the JSON produced by the C++ command handler in
// src/toi/app/src/application_commands.cpp. They are the single source of
// truth shared by the Bun shell (which routes commands into the native core)
// and the Solid UI (which issues them over RPC). Keep them in sync with the
// C++ `to_json` overloads.

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

export type GrowthPreviewMeshStats = {
	chain_count: number;
	mesh_count: number;
	vertex_count: number;
	face_count: number;
};

export type GrowthPreviewStage = GrowthPreviewMeshStats & {
	usda: string;
	render_product_path: string;
	camera_path: string;
	asset_search_path: string;
	hdri_texture_path: string;
	width: number;
	height: number;
};

// inspect.snapshot only carries the heavy USDA text when include_usda is set.
export type PreviewStats = Omit<GrowthPreviewStage, "usda"> & { usda?: string };

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

export type InspectSnapshot = {
	state: AppState;
	prototype_tree: PrototypeTree;
	growth_snapshot_summary: GrowthSnapshotSummary;
	preview_stats: PreviewStats;
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
	"inspect.snapshot": { params: { include_usda?: boolean }; result: InspectSnapshot };
	"module.list_prototypes": { params: NoParams; result: PrototypeSummary[] };
	"module.set_active_prototype": { params: { prototype_id: number }; result: NoParams };
	"module.set_active_plant_type": { params: { plant_type_id: string }; result: NoParams };
	"module.set_age": { params: { age: number }; result: NoParams };
	"module.get_prototype_tree": { params: NoParams; result: PrototypeTree };
	"module.get_growth_snapshot_summary": { params: NoParams; result: GrowthSnapshotSummary };
	"module.get_growth_snapshot": { params: NoParams; result: GrowthSnapshot };
	"module.get_growth_preview_stage": { params: NoParams; result: GrowthPreviewStage };
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
	"viewport.orbit_camera": {
		params: { azimuth_delta_radians: number; elevation_delta_radians: number };
		result: NoParams;
	};
	"viewport.dolly_camera": { params: { radius_multiplier: number }; result: NoParams };
	"viewport.reset_camera": { params: NoParams; result: NoParams };
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
