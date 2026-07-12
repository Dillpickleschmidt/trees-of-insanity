#include "toi/project/project.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <nlohmann/json.hpp>
#include <utility>

namespace toi::project {
namespace {

using nlohmann::json;

constexpr int kProjectVersion = 2;
constexpr std::string_view kDefaultPlantTypeId = "plant-type-1";
constexpr char kDefaultPlantTypePreset = 'o';

[[nodiscard]] ProjectError make_error(ProjectError::Code code, std::string message);
[[nodiscard]] ProjectError invalid_project(std::string message);
[[nodiscard]] std::string plant_type_name_for_preset(char preset_key);
[[nodiscard]] json project_to_json(const Project& project);
[[nodiscard]] Result<Project> project_from_json(const json& value);
[[nodiscard]] bool viewport_is_valid(const ViewportState& viewport);

} // namespace

Result<Project> make_default_project(std::size_t default_prototype_id, std::string default_hdri_environment_id)
{
    if (default_hdri_environment_id.empty()) {
        return std::unexpected(invalid_project("default HDRI environment id cannot be empty"));
    }

    auto plant_type = create_plant_type_from_preset(std::string(kDefaultPlantTypeId), {}, kDefaultPlantTypePreset);
    if (!plant_type) {
        return std::unexpected(plant_type.error());
    }

    ViewportState viewport;
    viewport.active_hdri_environment_id = std::move(default_hdri_environment_id);

    Project project;
    project.version = kProjectVersion;
    project.plant_type_library.plant_types.push_back(std::move(*plant_type));
    project.active_workspace = Workspace::Module;
    project.module_workspace = {
        .prototype_id = default_prototype_id,
        .plant_type_id = std::string(kDefaultPlantTypeId),
        .physiological_age = 0.0F,
        .viewport = viewport,
    };
    project.plant_workspace = {
        .root_prototype_id = default_prototype_id,
        .plant_type_id = std::string(kDefaultPlantTypeId),
        .simulation_timestep = 1.0F,
        .diagnostics = {},
        .viewport = viewport,
    };
    project.ecosystem_workspace = {.viewport = viewport};
    return project;
}

Result<Project> load_project(const std::filesystem::path& path)
{
    std::ifstream file(path);
    if (!file) {
        return std::unexpected(make_error(ProjectError::Code::Io, "failed to read " + path.string()));
    }

    try {
        json value;
        file >> value;
        return project_from_json(value);
    } catch (const json::exception& error) {
        return std::unexpected(
            make_error(ProjectError::Code::Parse, "failed to parse " + path.string() + ": " + error.what()));
    }
}

Result<void> save_project(const std::filesystem::path& path, const Project& project)
{
    auto required_project = require_valid_project(project);
    if (!required_project) {
        return std::unexpected(required_project.error());
    }

    const auto temporary_path = path.string() + ".tmp";
    {
        std::ofstream file(temporary_path, std::ios::trunc);
        if (!file) {
            return std::unexpected(make_error(ProjectError::Code::Io, "failed to write " + temporary_path));
        }

        file << project_to_json(project).dump(2) << '\n';
        file.flush();
        if (!file) {
            return std::unexpected(make_error(ProjectError::Code::Io, "failed to flush " + temporary_path));
        }
    }

    std::error_code rename_error;
    std::filesystem::rename(temporary_path, path, rename_error);
    if (rename_error) {
        std::filesystem::remove(temporary_path);
        return std::unexpected(
            make_error(ProjectError::Code::Io, "failed to replace " + path.string() + ": " + rename_error.message()));
    }
    return {};
}

const PlantType* plant_type_by_id(const Project& project, std::string_view id)
{
    const auto found = std::ranges::find_if(project.plant_type_library.plant_types,
                                            [id](const PlantType& plant_type) { return plant_type.id == id; });
    return found == project.plant_type_library.plant_types.end() ? nullptr : &*found;
}

PlantType* plant_type_by_id(Project& project, std::string_view id)
{
    const auto found = std::ranges::find_if(project.plant_type_library.plant_types,
                                            [id](const PlantType& plant_type) { return plant_type.id == id; });
    return found == project.plant_type_library.plant_types.end() ? nullptr : &*found;
}

Result<PlantType> create_plant_type_from_preset(std::string id, std::string name, char preset_key)
{
    auto preset = growth::plant_type_preset_by_key(preset_key);
    if (!preset) {
        return std::unexpected(make_error(ProjectError::Code::UnknownPlantTypePreset,
                                          "unknown plant type preset " + std::string(1, preset_key)));
    }
    if (name.empty()) {
        name = plant_type_name_for_preset(preset_key);
    }
    return PlantType{
        .id = std::move(id),
        .name = std::move(name),
        .parameters = *preset,
    };
}

std::string next_plant_type_id(const PlantTypeLibrary& library)
{
    std::size_t next = library.plant_types.size() + 1;
    while (true) {
        const std::string candidate = "plant-type-" + std::to_string(next);
        const auto found = std::ranges::find_if(
            library.plant_types, [&candidate](const PlantType& plant_type) { return plant_type.id == candidate; });
        if (found == library.plant_types.end()) {
            return candidate;
        }
        ++next;
    }
}

Result<void> delete_plant_type(Project& project, std::string_view id)
{
    auto& plant_types = project.plant_type_library.plant_types;
    if (plant_types.size() <= 1) {
        return std::unexpected(invalid_project("cannot delete the last plant type"));
    }

    const auto found =
        std::ranges::find_if(plant_types, [id](const PlantType& plant_type) { return plant_type.id == id; });
    if (found == plant_types.end()) {
        return std::unexpected(invalid_project("unknown plant type id " + std::string(id)));
    }

    const bool module_selected = found->id == project.module_workspace.plant_type_id;
    const bool plant_selected = found->id == project.plant_workspace.plant_type_id;
    plant_types.erase(found);
    if (module_selected) {
        project.module_workspace.plant_type_id = plant_types.front().id;
    }
    if (plant_selected) {
        project.plant_workspace.plant_type_id = plant_types.front().id;
    }
    return {};
}

Result<void> require_valid_project(const Project& project)
{
    if (project.version != kProjectVersion) {
        return std::unexpected(invalid_project("unsupported project version"));
    }
    switch (project.active_workspace) {
    case Workspace::Module:
    case Workspace::Plant:
    case Workspace::Ecosystem:
        break;
    default:
        return std::unexpected(invalid_project("active workspace is invalid"));
    }
    if (project.plant_type_library.plant_types.empty()) {
        return std::unexpected(invalid_project("plant type library must contain at least one plant type"));
    }

    std::vector<std::string> ids;
    ids.reserve(project.plant_type_library.plant_types.size());
    for (const auto& plant_type : project.plant_type_library.plant_types) {
        if (plant_type.id.empty()) {
            return std::unexpected(invalid_project("plant type id cannot be empty"));
        }
        if (!growth::plant_type_parameters_are_valid(plant_type.parameters)) {
            return std::unexpected(invalid_project("plant type parameters are invalid"));
        }
        ids.push_back(plant_type.id);
    }
    std::ranges::sort(ids);
    if (std::ranges::adjacent_find(ids) != ids.end()) {
        return std::unexpected(invalid_project("plant type ids must be unique"));
    }

    if (plant_type_by_id(project, project.module_workspace.plant_type_id) == nullptr) {
        return std::unexpected(invalid_project("Module plant type id does not exist"));
    }
    if (plant_type_by_id(project, project.plant_workspace.plant_type_id) == nullptr) {
        return std::unexpected(invalid_project("Plant plant type id does not exist"));
    }
    if (!std::isfinite(project.module_workspace.physiological_age) ||
        project.module_workspace.physiological_age < 0.0F) {
        return std::unexpected(invalid_project("Module physiological age must be finite and non-negative"));
    }
    if (!std::isfinite(project.plant_workspace.simulation_timestep) ||
        project.plant_workspace.simulation_timestep <= 0.0F) {
        return std::unexpected(invalid_project("Plant simulation timestep must be finite and positive"));
    }
    if (!viewport_is_valid(project.module_workspace.viewport) ||
        !viewport_is_valid(project.plant_workspace.viewport) ||
        !viewport_is_valid(project.ecosystem_workspace.viewport)) {
        return std::unexpected(invalid_project("workspace viewport state is invalid"));
    }
    return {};
}

std::string_view to_string(Workspace workspace)
{
    switch (workspace) {
    case Workspace::Module:
        return "module";
    case Workspace::Plant:
        return "plant";
    case Workspace::Ecosystem:
        return "ecosystem";
    }
    return "unknown";
}

namespace {

[[nodiscard]] ProjectError make_error(ProjectError::Code code, std::string message)
{
    return {.code = code, .message = std::move(message)};
}

[[nodiscard]] ProjectError invalid_project(std::string message)
{
    return make_error(ProjectError::Code::InvalidProject, std::move(message));
}

[[nodiscard]] std::string plant_type_name_for_preset(char preset_key)
{
    if (preset_key >= 'a' && preset_key <= 'z') {
        preset_key = static_cast<char>('A' + (preset_key - 'a'));
    }
    return "Plant Type " + std::string(1, preset_key);
}

[[nodiscard]] bool viewport_is_valid(const ViewportState& viewport)
{
    return !viewport.active_hdri_environment_id.empty() && std::isfinite(viewport.orbit.target.x) &&
           std::isfinite(viewport.orbit.target.y) && std::isfinite(viewport.orbit.target.z) &&
           std::isfinite(viewport.orbit.radius) && viewport.orbit.radius > 0.0F &&
           std::isfinite(viewport.orbit.azimuth_radians) && std::isfinite(viewport.orbit.elevation_radians);
}

[[nodiscard]] json optional_float_to_json(std::optional<float> value)
{
    return value ? json(*value) : json(nullptr);
}

[[nodiscard]] Result<std::optional<float>> optional_float_from_json(const json& object, std::string_view key)
{
    if (!object.contains(key)) {
        return std::unexpected(invalid_project("missing parameter " + std::string(key)));
    }
    const auto& value = object.at(key);
    if (value.is_null()) {
        return std::optional<float>{};
    }
    if (!value.is_number()) {
        return std::unexpected(invalid_project("parameter " + std::string(key) + " must be numeric or null"));
    }
    return value.get<float>();
}

[[nodiscard]] Result<float> float_from_json(const json& object, std::string_view key)
{
    if (!object.contains(key) || !object.at(key).is_number()) {
        return std::unexpected(invalid_project(std::string(key) + " must be numeric"));
    }
    return object.at(key).get<float>();
}

[[nodiscard]] Result<bool> bool_from_json(const json& object, std::string_view key)
{
    if (!object.contains(key) || !object.at(key).is_boolean()) {
        return std::unexpected(invalid_project(std::string(key) + " must be boolean"));
    }
    return object.at(key).get<bool>();
}

[[nodiscard]] Result<std::string> string_from_json(const json& object, std::string_view key)
{
    if (!object.contains(key) || !object.at(key).is_string()) {
        return std::unexpected(invalid_project(std::string(key) + " must be a string"));
    }
    return object.at(key).get<std::string>();
}

[[nodiscard]] Result<std::size_t> size_from_json(const json& object, std::string_view key)
{
    if (!object.contains(key) || !object.at(key).is_number_unsigned()) {
        return std::unexpected(invalid_project(std::string(key) + " must be an unsigned integer"));
    }
    return object.at(key).get<std::size_t>();
}

[[nodiscard]] json plant_type_parameters_to_json(const growth::PlantTypeParameters& parameters)
{
    return json{
        {"plant_max_age", parameters.plant_max_age},
        {"root_max_vigor", parameters.root_max_vigor},
        {"plant_growth_rate", parameters.plant_growth_rate},
        {"apical_control", parameters.apical_control},
        {"mature_apical_control", optional_float_to_json(parameters.mature_apical_control)},
        {"determinacy", parameters.determinacy},
        {"mature_determinacy", optional_float_to_json(parameters.mature_determinacy)},
        {"flowering_age", parameters.flowering_age},
        {"tropism_angle", parameters.tropism_angle},
        {"tropism_weight", parameters.tropism_weight},
        {"tropism_strength", parameters.tropism_strength},
        {"terminal_thickness", parameters.terminal_thickness},
        {"length_growth_scale", parameters.length_growth_scale},
    };
}

[[nodiscard]] Result<growth::PlantTypeParameters> plant_type_parameters_from_json(const json& value)
{
    if (!value.is_object()) {
        return std::unexpected(invalid_project("plant type parameters must be an object"));
    }

    auto plant_max_age = float_from_json(value, "plant_max_age");
    auto root_max_vigor = float_from_json(value, "root_max_vigor");
    auto plant_growth_rate = float_from_json(value, "plant_growth_rate");
    auto apical_control = float_from_json(value, "apical_control");
    auto mature_apical_control = optional_float_from_json(value, "mature_apical_control");
    auto determinacy = float_from_json(value, "determinacy");
    auto mature_determinacy = optional_float_from_json(value, "mature_determinacy");
    auto flowering_age = float_from_json(value, "flowering_age");
    auto tropism_angle = float_from_json(value, "tropism_angle");
    auto tropism_weight = float_from_json(value, "tropism_weight");
    auto tropism_strength = float_from_json(value, "tropism_strength");
    auto terminal_thickness = float_from_json(value, "terminal_thickness");
    auto length_growth_scale = float_from_json(value, "length_growth_scale");
    if (!plant_max_age)
        return std::unexpected(plant_max_age.error());
    if (!root_max_vigor)
        return std::unexpected(root_max_vigor.error());
    if (!plant_growth_rate)
        return std::unexpected(plant_growth_rate.error());
    if (!apical_control)
        return std::unexpected(apical_control.error());
    if (!mature_apical_control)
        return std::unexpected(mature_apical_control.error());
    if (!determinacy)
        return std::unexpected(determinacy.error());
    if (!mature_determinacy)
        return std::unexpected(mature_determinacy.error());
    if (!flowering_age)
        return std::unexpected(flowering_age.error());
    if (!tropism_angle)
        return std::unexpected(tropism_angle.error());
    if (!tropism_weight)
        return std::unexpected(tropism_weight.error());
    if (!tropism_strength)
        return std::unexpected(tropism_strength.error());
    if (!terminal_thickness)
        return std::unexpected(terminal_thickness.error());
    if (!length_growth_scale)
        return std::unexpected(length_growth_scale.error());

    growth::PlantTypeParameters parameters{
        .plant_max_age = *plant_max_age,
        .root_max_vigor = *root_max_vigor,
        .plant_growth_rate = *plant_growth_rate,
        .apical_control = *apical_control,
        .mature_apical_control = *mature_apical_control,
        .determinacy = *determinacy,
        .mature_determinacy = *mature_determinacy,
        .flowering_age = *flowering_age,
        .tropism_angle = *tropism_angle,
        .tropism_weight = *tropism_weight,
        .tropism_strength = *tropism_strength,
        .terminal_thickness = *terminal_thickness,
        .length_growth_scale = *length_growth_scale,
    };
    if (!growth::plant_type_parameters_are_valid(parameters)) {
        return std::unexpected(invalid_project("plant type parameters are invalid"));
    }
    return parameters;
}

[[nodiscard]] json plant_type_to_json(const PlantType& plant_type)
{
    return json{
        {"id", plant_type.id},
        {"name", plant_type.name},
        {"parameters", plant_type_parameters_to_json(plant_type.parameters)},
    };
}

[[nodiscard]] Result<PlantType> plant_type_from_json(const json& value)
{
    if (!value.is_object()) {
        return std::unexpected(invalid_project("plant type must be an object"));
    }
    auto id = string_from_json(value, "id");
    auto name = string_from_json(value, "name");
    if (!id)
        return std::unexpected(id.error());
    if (!name)
        return std::unexpected(name.error());
    if (!value.contains("parameters")) {
        return std::unexpected(invalid_project("plant type missing parameters"));
    }
    auto parameters = plant_type_parameters_from_json(value.at("parameters"));
    if (!parameters) {
        return std::unexpected(parameters.error());
    }
    return PlantType{.id = std::move(*id), .name = std::move(*name), .parameters = *parameters};
}

[[nodiscard]] json viewport_to_json(const ViewportState& viewport)
{
    return json{
        {"guides_visible", viewport.guides_visible},
        {"world_origin_axes_visible", viewport.world_origin_axes_visible},
        {"hdri_backdrop_visible", viewport.hdri_backdrop_visible},
        {"active_hdri_environment_id", viewport.active_hdri_environment_id},
        {"orbit",
         {
             {"target",
              {{"x", viewport.orbit.target.x}, {"y", viewport.orbit.target.y}, {"z", viewport.orbit.target.z}}},
             {"radius", viewport.orbit.radius},
             {"azimuth_radians", viewport.orbit.azimuth_radians},
             {"elevation_radians", viewport.orbit.elevation_radians},
         }},
    };
}

[[nodiscard]] Result<ViewportState> viewport_from_json(const json& value)
{
    if (!value.is_object()) {
        return std::unexpected(invalid_project("viewport must be an object"));
    }
    auto guides = bool_from_json(value, "guides_visible");
    auto axes = bool_from_json(value, "world_origin_axes_visible");
    auto backdrop = bool_from_json(value, "hdri_backdrop_visible");
    auto hdri = string_from_json(value, "active_hdri_environment_id");
    if (!guides)
        return std::unexpected(guides.error());
    if (!axes)
        return std::unexpected(axes.error());
    if (!backdrop)
        return std::unexpected(backdrop.error());
    if (!hdri)
        return std::unexpected(hdri.error());
    if (!value.contains("orbit") || !value.at("orbit").is_object()) {
        return std::unexpected(invalid_project("viewport orbit must be an object"));
    }
    const auto& orbit = value.at("orbit");
    if (!orbit.contains("target") || !orbit.at("target").is_object()) {
        return std::unexpected(invalid_project("viewport orbit target must be an object"));
    }
    const auto& target = orbit.at("target");
    auto x = float_from_json(target, "x");
    auto y = float_from_json(target, "y");
    auto z = float_from_json(target, "z");
    auto radius = float_from_json(orbit, "radius");
    auto azimuth = float_from_json(orbit, "azimuth_radians");
    auto elevation = float_from_json(orbit, "elevation_radians");
    if (!x)
        return std::unexpected(x.error());
    if (!y)
        return std::unexpected(y.error());
    if (!z)
        return std::unexpected(z.error());
    if (!radius)
        return std::unexpected(radius.error());
    if (!azimuth)
        return std::unexpected(azimuth.error());
    if (!elevation)
        return std::unexpected(elevation.error());
    return ViewportState{
        .guides_visible = *guides,
        .world_origin_axes_visible = *axes,
        .hdri_backdrop_visible = *backdrop,
        .active_hdri_environment_id = std::move(*hdri),
        .orbit = {.target = {.x = *x, .y = *y, .z = *z},
                  .radius = *radius,
                  .azimuth_radians = *azimuth,
                  .elevation_radians = *elevation},
    };
}

[[nodiscard]] json diagnostics_to_json(const PlantDiagnostics& diagnostics)
{
    return json{
        {"module_diagnostic_labels_visible", diagnostics.module_diagnostic_labels_visible},
        {"direct_light_bounding_spheres_visible", diagnostics.direct_light_bounding_spheres_visible},
        {"accumulated_light_flow_visible", diagnostics.accumulated_light_flow_visible},
        {"vigor_flow_visible", diagnostics.vigor_flow_visible},
        {"mature_terminal_markers_visible", diagnostics.mature_terminal_markers_visible},
    };
}

[[nodiscard]] Result<PlantDiagnostics> diagnostics_from_json(const json& value)
{
    if (!value.is_object()) {
        return std::unexpected(invalid_project("Plant diagnostics must be an object"));
    }
    auto labels = bool_from_json(value, "module_diagnostic_labels_visible");
    auto spheres = bool_from_json(value, "direct_light_bounding_spheres_visible");
    auto light = bool_from_json(value, "accumulated_light_flow_visible");
    auto vigor = bool_from_json(value, "vigor_flow_visible");
    auto terminals = bool_from_json(value, "mature_terminal_markers_visible");
    if (!labels)
        return std::unexpected(labels.error());
    if (!spheres)
        return std::unexpected(spheres.error());
    if (!light)
        return std::unexpected(light.error());
    if (!vigor)
        return std::unexpected(vigor.error());
    if (!terminals)
        return std::unexpected(terminals.error());
    return PlantDiagnostics{
        .module_diagnostic_labels_visible = *labels,
        .direct_light_bounding_spheres_visible = *spheres,
        .accumulated_light_flow_visible = *light,
        .vigor_flow_visible = *vigor,
        .mature_terminal_markers_visible = *terminals,
    };
}

[[nodiscard]] json project_to_json(const Project& project)
{
    json plant_types = json::array();
    for (const auto& plant_type : project.plant_type_library.plant_types) {
        plant_types.push_back(plant_type_to_json(plant_type));
    }
    return json{
        {"version", project.version},
        {"plant_type_library", {{"plant_types", plant_types}}},
        {"active_workspace", to_string(project.active_workspace)},
        {"module_workspace",
         {
             {"prototype_id", project.module_workspace.prototype_id},
             {"plant_type_id", project.module_workspace.plant_type_id},
             {"physiological_age", project.module_workspace.physiological_age},
             {"viewport", viewport_to_json(project.module_workspace.viewport)},
         }},
        {"plant_workspace",
         {
             {"root_prototype_id", project.plant_workspace.root_prototype_id},
             {"plant_type_id", project.plant_workspace.plant_type_id},
             {"simulation_timestep", project.plant_workspace.simulation_timestep},
             {"diagnostics", diagnostics_to_json(project.plant_workspace.diagnostics)},
             {"viewport", viewport_to_json(project.plant_workspace.viewport)},
         }},
        {"ecosystem_workspace", {{"viewport", viewport_to_json(project.ecosystem_workspace.viewport)}}},
    };
}

[[nodiscard]] Result<Workspace> workspace_from_json(const json& value)
{
    if (!value.is_string()) {
        return std::unexpected(invalid_project("active_workspace must be a string"));
    }
    const auto workspace = value.get<std::string>();
    if (workspace == "module")
        return Workspace::Module;
    if (workspace == "plant")
        return Workspace::Plant;
    if (workspace == "ecosystem")
        return Workspace::Ecosystem;
    return std::unexpected(invalid_project("unknown active workspace " + workspace));
}

[[nodiscard]] Result<Project> project_from_json(const json& value)
{
    if (!value.is_object()) {
        return std::unexpected(invalid_project("project root must be an object"));
    }
    if (!value.contains("version") || !value.at("version").is_number_integer()) {
        return std::unexpected(invalid_project("project version must be an integer"));
    }

    Project project;
    project.version = value.at("version").get<int>();
    if (project.version != kProjectVersion) {
        return std::unexpected(invalid_project("unsupported project version " + std::to_string(project.version)));
    }
    if (!value.contains("plant_type_library") || !value.at("plant_type_library").is_object()) {
        return std::unexpected(invalid_project("project missing plant_type_library"));
    }
    const auto& library = value.at("plant_type_library");
    if (!library.contains("plant_types") || !library.at("plant_types").is_array()) {
        return std::unexpected(invalid_project("plant_type_library missing plant_types"));
    }
    for (const auto& plant_type_value : library.at("plant_types")) {
        auto plant_type = plant_type_from_json(plant_type_value);
        if (!plant_type) {
            return std::unexpected(plant_type.error());
        }
        project.plant_type_library.plant_types.push_back(std::move(*plant_type));
    }

    if (!value.contains("active_workspace")) {
        return std::unexpected(invalid_project("project missing active_workspace"));
    }
    auto active_workspace = workspace_from_json(value.at("active_workspace"));
    if (!active_workspace) {
        return std::unexpected(active_workspace.error());
    }
    project.active_workspace = *active_workspace;

    if (!value.contains("module_workspace") || !value.at("module_workspace").is_object()) {
        return std::unexpected(invalid_project("project missing module_workspace"));
    }
    const auto& module = value.at("module_workspace");
    auto module_prototype = size_from_json(module, "prototype_id");
    auto module_plant_type = string_from_json(module, "plant_type_id");
    auto module_age = float_from_json(module, "physiological_age");
    if (!module_prototype)
        return std::unexpected(module_prototype.error());
    if (!module_plant_type)
        return std::unexpected(module_plant_type.error());
    if (!module_age)
        return std::unexpected(module_age.error());
    if (!module.contains("viewport"))
        return std::unexpected(invalid_project("Module workspace missing viewport"));
    auto module_viewport = viewport_from_json(module.at("viewport"));
    if (!module_viewport)
        return std::unexpected(module_viewport.error());
    project.module_workspace = {.prototype_id = *module_prototype,
                                .plant_type_id = std::move(*module_plant_type),
                                .physiological_age = *module_age,
                                .viewport = std::move(*module_viewport)};

    if (!value.contains("plant_workspace") || !value.at("plant_workspace").is_object()) {
        return std::unexpected(invalid_project("project missing plant_workspace"));
    }
    const auto& plant = value.at("plant_workspace");
    auto root_prototype = size_from_json(plant, "root_prototype_id");
    auto plant_plant_type = string_from_json(plant, "plant_type_id");
    auto timestep = float_from_json(plant, "simulation_timestep");
    if (!root_prototype)
        return std::unexpected(root_prototype.error());
    if (!plant_plant_type)
        return std::unexpected(plant_plant_type.error());
    if (!timestep)
        return std::unexpected(timestep.error());
    if (!plant.contains("diagnostics"))
        return std::unexpected(invalid_project("Plant workspace missing diagnostics"));
    auto diagnostics = diagnostics_from_json(plant.at("diagnostics"));
    if (!diagnostics)
        return std::unexpected(diagnostics.error());
    if (!plant.contains("viewport"))
        return std::unexpected(invalid_project("Plant workspace missing viewport"));
    auto plant_viewport = viewport_from_json(plant.at("viewport"));
    if (!plant_viewport)
        return std::unexpected(plant_viewport.error());
    project.plant_workspace = {.root_prototype_id = *root_prototype,
                               .plant_type_id = std::move(*plant_plant_type),
                               .simulation_timestep = *timestep,
                               .diagnostics = *diagnostics,
                               .viewport = std::move(*plant_viewport)};

    if (!value.contains("ecosystem_workspace") || !value.at("ecosystem_workspace").is_object()) {
        return std::unexpected(invalid_project("project missing ecosystem_workspace"));
    }
    const auto& ecosystem = value.at("ecosystem_workspace");
    if (!ecosystem.contains("viewport"))
        return std::unexpected(invalid_project("Ecosystem workspace missing viewport"));
    auto ecosystem_viewport = viewport_from_json(ecosystem.at("viewport"));
    if (!ecosystem_viewport)
        return std::unexpected(ecosystem_viewport.error());
    project.ecosystem_workspace = {.viewport = std::move(*ecosystem_viewport)};

    auto required_project = require_valid_project(project);
    if (!required_project) {
        return std::unexpected(required_project.error());
    }
    return project;
}

} // namespace
} // namespace toi::project
