#include "toi/project/project.hpp"

#include <algorithm>
#include <fstream>
#include <nlohmann/json.hpp>
#include <utility>

namespace toi::project {
namespace {

using nlohmann::json;

constexpr int kProjectVersion = 1;
constexpr std::string_view kDefaultPlantTypeId = "plant-type-1";
constexpr char kDefaultPlantTypePreset = 'o';

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

[[nodiscard]] json optional_float_to_json(std::optional<float> value)
{
    if (!value) {
        return nullptr;
    }
    return *value;
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
    if (!object.contains(key)) {
        return std::unexpected(invalid_project("missing parameter " + std::string(key)));
    }
    const auto& value = object.at(key);
    if (!value.is_number()) {
        return std::unexpected(invalid_project("parameter " + std::string(key) + " must be numeric"));
    }
    return value.get<float>();
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
    if (!plant_max_age)
        return std::unexpected(plant_max_age.error());
    auto root_max_vigor = float_from_json(value, "root_max_vigor");
    if (!root_max_vigor)
        return std::unexpected(root_max_vigor.error());
    auto plant_growth_rate = float_from_json(value, "plant_growth_rate");
    if (!plant_growth_rate)
        return std::unexpected(plant_growth_rate.error());
    auto apical_control = float_from_json(value, "apical_control");
    if (!apical_control)
        return std::unexpected(apical_control.error());
    auto mature_apical_control = optional_float_from_json(value, "mature_apical_control");
    if (!mature_apical_control)
        return std::unexpected(mature_apical_control.error());
    auto determinacy = float_from_json(value, "determinacy");
    if (!determinacy)
        return std::unexpected(determinacy.error());
    auto mature_determinacy = optional_float_from_json(value, "mature_determinacy");
    if (!mature_determinacy)
        return std::unexpected(mature_determinacy.error());
    auto flowering_age = float_from_json(value, "flowering_age");
    if (!flowering_age)
        return std::unexpected(flowering_age.error());
    auto tropism_angle = float_from_json(value, "tropism_angle");
    if (!tropism_angle)
        return std::unexpected(tropism_angle.error());
    auto tropism_weight = float_from_json(value, "tropism_weight");
    if (!tropism_weight)
        return std::unexpected(tropism_weight.error());
    auto tropism_strength = float_from_json(value, "tropism_strength");
    if (!tropism_strength)
        return std::unexpected(tropism_strength.error());
    auto terminal_thickness = float_from_json(value, "terminal_thickness");
    if (!terminal_thickness)
        return std::unexpected(terminal_thickness.error());
    auto length_growth_scale = float_from_json(value, "length_growth_scale");
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
    if (!value.contains("id") || !value.at("id").is_string()) {
        return std::unexpected(invalid_project("plant type id must be a string"));
    }
    if (!value.contains("name") || !value.at("name").is_string()) {
        return std::unexpected(invalid_project("plant type name must be a string"));
    }

    if (!value.contains("parameters")) {
        return std::unexpected(invalid_project("plant type missing parameters"));
    }
    auto parameters = plant_type_parameters_from_json(value.at("parameters"));
    if (!parameters) {
        return std::unexpected(parameters.error());
    }

    return PlantType{
        .id = value.at("id").get<std::string>(),
        .name = value.at("name").get<std::string>(),
        .parameters = *parameters,
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
        {"plant_type_library",
         {
             {"plant_types", plant_types},
             {"active_plant_type_id", project.plant_type_library.active_plant_type_id},
         }},
        {"active_branch_module_prototype_id", project.active_branch_module_prototype_id},
    };
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
    if (!library.contains("active_plant_type_id") || !library.at("active_plant_type_id").is_string()) {
        return std::unexpected(invalid_project("plant_type_library missing active_plant_type_id"));
    }

    project.plant_type_library.active_plant_type_id = library.at("active_plant_type_id").get<std::string>();
    for (const auto& plant_type_value : library.at("plant_types")) {
        auto plant_type = plant_type_from_json(plant_type_value);
        if (!plant_type) {
            return std::unexpected(plant_type.error());
        }
        project.plant_type_library.plant_types.push_back(std::move(*plant_type));
    }

    if (!value.contains("active_branch_module_prototype_id") ||
        !value.at("active_branch_module_prototype_id").is_number_unsigned()) {
        return std::unexpected(invalid_project("project missing active_branch_module_prototype_id"));
    }
    project.active_branch_module_prototype_id = value.at("active_branch_module_prototype_id").get<std::size_t>();

    auto required_project = require_valid_project(project);
    if (!required_project) {
        return std::unexpected(required_project.error());
    }
    return project;
}

} // namespace

Result<Project> make_default_project(std::size_t default_prototype_id)
{
    auto plant_type = create_plant_type_from_preset(std::string(kDefaultPlantTypeId), {}, kDefaultPlantTypePreset);
    if (!plant_type) {
        return std::unexpected(plant_type.error());
    }

    Project project;
    project.version = kProjectVersion;
    project.plant_type_library.plant_types.push_back(std::move(*plant_type));
    project.plant_type_library.active_plant_type_id = std::string(kDefaultPlantTypeId);
    project.active_branch_module_prototype_id = default_prototype_id;
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

Result<Project> load_or_create_project(const std::filesystem::path& path, std::size_t default_prototype_id)
{
    if (std::filesystem::exists(path)) {
        return load_project(path);
    }

    auto project = make_default_project(default_prototype_id);
    if (!project) {
        return std::unexpected(project.error());
    }
    auto saved = save_project(path, *project);
    if (!saved) {
        return std::unexpected(saved.error());
    }
    return project;
}

const PlantType* active_plant_type(const Project& project)
{
    return plant_type_by_id(project, project.plant_type_library.active_plant_type_id);
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

    const bool deleting_active = found->id == project.plant_type_library.active_plant_type_id;
    plant_types.erase(found);
    if (deleting_active) {
        project.plant_type_library.active_plant_type_id = plant_types.front().id;
    }
    return {};
}

Result<void> require_valid_project(const Project& project)
{
    if (project.version != kProjectVersion) {
        return std::unexpected(invalid_project("unsupported project version"));
    }
    if (project.plant_type_library.plant_types.empty()) {
        return std::unexpected(invalid_project("plant type library must contain at least one plant type"));
    }
    if (project.plant_type_library.active_plant_type_id.empty()) {
        return std::unexpected(invalid_project("active plant type id cannot be empty"));
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

    if (active_plant_type(project) == nullptr) {
        return std::unexpected(invalid_project("active plant type id does not exist"));
    }
    return {};
}

} // namespace toi::project
