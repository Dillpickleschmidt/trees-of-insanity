#include "desktop_actions.hpp"

#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace toi::desktop {

using namespace model;

namespace {

using nlohmann::json;

[[nodiscard]] json response_ok(json id, json result)
{
    return json{{"id", std::move(id)}, {"ok", true}, {"result", std::move(result)}};
}

[[nodiscard]] json response_error(json id, std::string message)
{
    return json{{"id", std::move(id)}, {"ok", false}, {"error", std::move(message)}};
}

[[nodiscard]] std::string application_error_code(ApplicationError::Code code)
{
    switch (code) {
    case ApplicationError::Code::Import:
        return "import";
    case ApplicationError::Code::Project:
        return "project";
    case ApplicationError::Code::Growth:
        return "growth";
    case ApplicationError::Code::InvalidCommand:
        return "invalid_command";
    case ApplicationError::Code::NotFound:
        return "not_found";
    }
    std::unreachable();
}

[[nodiscard]] json response_error(json id, const ApplicationError& error)
{
    return json{
        {"id", std::move(id)}, {"ok", false}, {"error", error.message}, {"code", application_error_code(error.code)}};
}

[[nodiscard]] json require_params_object(const json& request)
{
    if (!request.contains("params") || request.at("params").is_null()) {
        return json::object();
    }
    if (!request.at("params").is_object()) {
        throw std::invalid_argument("params must be an object");
    }
    return request.at("params");
}

[[nodiscard]] float json_float(const json& params, std::string_view key)
{
    if (!params.contains(key) || !params.at(key).is_number()) {
        throw std::invalid_argument(std::string(key) + " must be numeric");
    }
    return params.at(key).get<float>();
}

[[nodiscard]] std::string json_string(const json& params, std::string_view key)
{
    if (!params.contains(key) || !params.at(key).is_string()) {
        throw std::invalid_argument(std::string(key) + " must be a string");
    }
    return params.at(key).get<std::string>();
}

[[nodiscard]] std::size_t json_size(const json& params, std::string_view key)
{
    if (!params.contains(key) || !params.at(key).is_number_unsigned()) {
        throw std::invalid_argument(std::string(key) + " must be an unsigned integer");
    }
    return params.at(key).get<std::size_t>();
}

[[nodiscard]] bool optional_json_bool(const json& params, std::string_view key, bool default_value)
{
    if (!params.contains(key)) {
        return default_value;
    }
    if (!params.at(key).is_boolean()) {
        throw std::invalid_argument(std::string(key) + " must be a boolean");
    }
    return params.at(key).get<bool>();
}

[[nodiscard]] char plant_type_preset_key(const json& params)
{
    if (!params.contains("preset_key")) {
        return 'o';
    }
    if (!params.at("preset_key").is_string()) {
        throw std::invalid_argument("preset_key must be a single-letter string");
    }
    const auto value = params.at("preset_key").get<std::string>();
    if (value.size() != 1) {
        throw std::invalid_argument("preset_key must be a single-letter string");
    }
    return value.front();
}

[[nodiscard]] json prototype_tree_item_to_json(const PrototypeTreeItem& item)
{
    json children = json::array();
    for (const auto& child : item.children) {
        children.push_back(prototype_tree_item_to_json(child));
    }
    return json{
        {"kind", to_string(item.kind)},
        {"id", item.id},
        {"label", item.label},
        {"children", children},
    };
}

[[nodiscard]] json vec3_to_json(growth::Vec3 value)
{
    return json::array({value.x, value.y, value.z});
}

[[nodiscard]] json plant_type_parameters_to_json(const growth::PlantTypeParameters& parameters)
{
    auto optional_value = [](std::optional<float> value) -> json {
        if (!value) {
            return nullptr;
        }
        return *value;
    };

    return json{
        {"plant_max_age", parameters.plant_max_age},
        {"root_max_vigor", parameters.root_max_vigor},
        {"plant_growth_rate", parameters.plant_growth_rate},
        {"apical_control", parameters.apical_control},
        {"mature_apical_control", optional_value(parameters.mature_apical_control)},
        {"determinacy", parameters.determinacy},
        {"mature_determinacy", optional_value(parameters.mature_determinacy)},
        {"flowering_age", parameters.flowering_age},
        {"tropism_angle", parameters.tropism_angle},
        {"tropism_weight", parameters.tropism_weight},
        {"tropism_strength", parameters.tropism_strength},
        {"terminal_thickness", parameters.terminal_thickness},
        {"length_growth_scale", parameters.length_growth_scale},
    };
}

[[nodiscard]] json parameter_descriptors_to_json()
{
    json descriptors = json::array();
    for (const auto& descriptor : growth::plant_type_parameter_descriptors()) {
        descriptors.push_back({
            {"key", descriptor.key},
            {"min", descriptor.min ? json(*descriptor.min) : json(nullptr)},
            {"max", descriptor.max ? json(*descriptor.max) : json(nullptr)},
        });
    }
    return descriptors;
}

[[nodiscard]] std::optional<float> optional_parameter_value(const json& parameters, std::string_view key,
                                                            std::optional<float> existing)
{
    if (!parameters.contains(key)) {
        return existing;
    }
    if (parameters.at(key).is_null()) {
        return std::nullopt;
    }
    if (!parameters.at(key).is_number()) {
        throw std::invalid_argument(std::string(key) + " must be numeric or null");
    }
    return parameters.at(key).get<float>();
}

[[nodiscard]] float parameter_value(const json& parameters, std::string_view key, float existing)
{
    if (!parameters.contains(key)) {
        return existing;
    }
    if (!parameters.at(key).is_number()) {
        throw std::invalid_argument(std::string(key) + " must be numeric");
    }
    return parameters.at(key).get<float>();
}

void apply_parameter_update(project::PlantType& plant_type, const json& parameters)
{
    if (!parameters.is_object()) {
        throw std::invalid_argument("parameters must be an object");
    }

    auto& p = plant_type.parameters;
    p.plant_max_age = parameter_value(parameters, "plant_max_age", p.plant_max_age);
    p.root_max_vigor = parameter_value(parameters, "root_max_vigor", p.root_max_vigor);
    p.plant_growth_rate = parameter_value(parameters, "plant_growth_rate", p.plant_growth_rate);
    p.apical_control = parameter_value(parameters, "apical_control", p.apical_control);
    p.mature_apical_control = optional_parameter_value(parameters, "mature_apical_control", p.mature_apical_control);
    p.determinacy = parameter_value(parameters, "determinacy", p.determinacy);
    p.mature_determinacy = optional_parameter_value(parameters, "mature_determinacy", p.mature_determinacy);
    p.flowering_age = parameter_value(parameters, "flowering_age", p.flowering_age);
    p.tropism_angle = parameter_value(parameters, "tropism_angle", p.tropism_angle);
    p.tropism_weight = parameter_value(parameters, "tropism_weight", p.tropism_weight);
    p.tropism_strength = parameter_value(parameters, "tropism_strength", p.tropism_strength);
    p.terminal_thickness = parameter_value(parameters, "terminal_thickness", p.terminal_thickness);
    p.length_growth_scale = parameter_value(parameters, "length_growth_scale", p.length_growth_scale);
}

} // namespace

bool action_changes_preview(std::string_view method)
{
    return method == "module.set_age" || method == "module.set_active_prototype" ||
           method == "module.set_active_plant_type" || method == "workspace.set" ||
           method == "plant_types.create" || method == "plant_types.update" || method == "plant_types.delete" ||
           method == "viewport.set_preferences";
}

[[nodiscard]] static json to_json(const ViewportPreferences& preferences)
{
    return json{
        {"guides_visible", preferences.guides_visible},
        {"world_origin_axes_visible", preferences.world_origin_axes_visible},
        {"hdri_backdrop_visible", preferences.hdri_backdrop_visible},
        {"active_hdri_environment_id", preferences.active_hdri_environment_id},
    };
}

[[nodiscard]] static json to_json(const AppStateView& state)
{
    json prototypes = json::array();
    for (const auto& prototype : state.prototypes) {
        prototypes.push_back({
            {"id", prototype.id},
            {"name", prototype.name},
            {"node_count", prototype.node_count},
            {"segment_count", prototype.segment_count},
        });
    }

    json plant_types = json::array();
    for (const auto& plant_type : state.plant_types) {
        plant_types.push_back({
            {"id", plant_type.id},
            {"name", plant_type.name},
        });
    }

    json workspace_previews = json::array();
    for (const auto& preview : state.workspace_previews) {
        workspace_previews.push_back({
            {"workspace", preview.workspace},
            {"implemented", preview.implemented},
        });
    }

    return json{
        {"active_workspace", state.active_workspace},
        {"workspace_previews", workspace_previews},
        {"prototypes", prototypes},
        {"active_prototype_id", state.active_prototype_id},
        {"plant_types", plant_types},
        {"active_plant_type_id", state.active_plant_type_id},
        {"module_physiological_age", state.module_physiological_age},
        {"fully_grown_age", state.fully_grown_age},
        {"plant_type_parameter_descriptors", parameter_descriptors_to_json()},
    };
}

[[nodiscard]] static json to_json(const PrototypeTreeView& tree)
{
    return json{{"root", prototype_tree_item_to_json(tree.root)}};
}

[[nodiscard]] static json to_json(const GrowthSnapshotSummary& summary)
{
    return json{
        {"module_physiological_age", summary.module_physiological_age},
        {"growth_rate", summary.growth_rate},
        {"visible_segment_count", summary.visible_segment_count},
        {"growing_segment_count", summary.growing_segment_count},
        {"mature_segment_count", summary.mature_segment_count},
        {"max_diameter", summary.max_diameter},
    };
}

[[nodiscard]] static json to_json(const growth::GrowthSnapshot& snapshot)
{
    json segments = json::array();
    for (const auto& segment : snapshot.segments) {
        segments.push_back({
            {"source_segment_id", segment.source_segment_id},
            {"parent_position", vec3_to_json(segment.parent_position)},
            {"child_position", vec3_to_json(segment.child_position)},
            {"diameter", segment.diameter},
            {"state", growth::to_string(segment.state)},
        });
    }

    return json{
        {"module_physiological_age", snapshot.module_physiological_age},
        {"growth_rate", snapshot.growth_rate},
        {"segments", segments},
    };
}

[[nodiscard]] static json to_json(const project::PlantType& plant_type)
{
    return json{
        {"id", plant_type.id},
        {"name", plant_type.name},
        {"parameters", plant_type_parameters_to_json(plant_type.parameters)},
    };
}

json dispatch_action(DesktopSession& session, const json& request)
{
    json id = request.contains("id") ? request.at("id") : nullptr;
    try {
        if (!request.is_object()) {
            return response_error(id, "request must be an object");
        }
        if (!request.contains("method") || !request.at("method").is_string()) {
            return response_error(id, "method must be a string");
        }

        const std::string method = request.at("method").get<std::string>();
        const json params = require_params_object(request);

        if (method == "app.get_state") {
            auto state = session.state();
            return state ? response_ok(id, to_json(*state)) : response_error(id, state.error());
        }
        if (method == "project.save") {
            auto saved = session.save_project();
            return saved ? response_ok(id, json::object()) : response_error(id, saved.error());
        }
        if (method == "module.list_prototypes") {
            auto state = session.state();
            if (!state)
                return response_error(id, state.error());
            return response_ok(id, to_json(*state).at("prototypes"));
        }
        if (method == "module.set_active_prototype") {
            auto result = session.set_active_prototype(json_size(params, "prototype_id"));
            return result ? response_ok(id, json::object()) : response_error(id, result.error());
        }
        if (method == "module.set_active_plant_type") {
            auto result = session.set_active_plant_type(json_string(params, "plant_type_id"));
            return result ? response_ok(id, json::object()) : response_error(id, result.error());
        }
        if (method == "module.set_age") {
            auto result = session.set_module_physiological_age(json_float(params, "age"));
            return result ? response_ok(id, json::object()) : response_error(id, result.error());
        }
        if (method == "module.get_prototype_tree") {
            auto tree = session.prototype_tree();
            return tree ? response_ok(id, to_json(*tree)) : response_error(id, tree.error());
        }
        if (method == "module.get_growth_snapshot_summary") {
            auto summary = session.growth_snapshot_summary();
            return summary ? response_ok(id, to_json(*summary)) : response_error(id, summary.error());
        }
        if (method == "module.get_growth_snapshot") {
            auto snapshot = session.growth_snapshot();
            return snapshot ? response_ok(id, to_json(*snapshot)) : response_error(id, snapshot.error());
        }
        if (method == "workspace.set") {
            auto result = session.set_active_workspace(json_string(params, "workspace"));
            return result ? response_ok(id, json::object()) : response_error(id, result.error());
        }
        if (method == "plant_types.list") {
            auto state = session.state();
            if (!state)
                return response_error(id, state.error());
            return response_ok(id, to_json(*state).at("plant_types"));
        }
        if (method == "plant_types.get") {
            auto plant_type = session.plant_type(json_string(params, "plant_type_id"));
            return plant_type ? response_ok(id, to_json(*plant_type)) : response_error(id, plant_type.error());
        }
        if (method == "plant_types.create") {
            auto plant_type = session.create_plant_type(json_string(params, "name"), plant_type_preset_key(params));
            return plant_type ? response_ok(id, to_json(*plant_type)) : response_error(id, plant_type.error());
        }
        if (method == "plant_types.delete") {
            auto result = session.delete_plant_type(json_string(params, "plant_type_id"));
            return result ? response_ok(id, json::object()) : response_error(id, result.error());
        }
        if (method == "plant_types.update") {
            const auto plant_type_id = json_string(params, "plant_type_id");
            auto existing = session.plant_type(plant_type_id);
            if (!existing) {
                return response_error(id, existing.error());
            }
            auto updated = *existing;
            if (params.contains("name")) {
                updated.name = json_string(params, "name");
            }
            if (params.contains("parameters")) {
                apply_parameter_update(updated, params.at("parameters"));
            }
            auto result = session.update_plant_type(std::move(updated));
            return result ? response_ok(id, json::object()) : response_error(id, result.error());
        }

        if (method == "viewport.get_preferences") {
            json environments = json::array();
            for (const auto& environment : session.hdri_environments()) {
                environments.push_back({
                    {"id", environment.id},
                    {"name", environment.name},
                    {"bundled", environment.bundled},
                });
            }
            return response_ok(id, json{
                                       {"preferences", to_json(session.viewport_preferences())},
                                       {"hdri_environments", environments},
                                   });
        }
        if (method == "viewport.set_preferences") {
            auto preferences = session.viewport_preferences();
            preferences.guides_visible = optional_json_bool(params, "guides_visible", preferences.guides_visible);
            preferences.world_origin_axes_visible =
                optional_json_bool(params, "world_origin_axes_visible", preferences.world_origin_axes_visible);
            preferences.hdri_backdrop_visible =
                optional_json_bool(params, "hdri_backdrop_visible", preferences.hdri_backdrop_visible);
            if (params.contains("active_hdri_environment_id")) {
                preferences.active_hdri_environment_id = json_string(params, "active_hdri_environment_id");
            }
            auto result = session.update_viewport_preferences(std::move(preferences));
            return result ? response_ok(id, json::object()) : response_error(id, result.error());
        }

        return response_error(id, "unknown method " + method);
    } catch (const std::exception& error) {
        return response_error(id, error.what());
    }
}

} // namespace toi::desktop
