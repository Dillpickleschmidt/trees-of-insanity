#include "desktop_actions.hpp"

#include <nlohmann/json.hpp>

#include <exception>
#include <string>
#include <utility>

namespace toi::desktop {

using namespace model;

namespace {

using nlohmann::json;

struct ActionResult {
    json response;
    bool preview_changed = false;
};

[[nodiscard]] ActionResult execute_action(DesktopSession& session, const json& request);
[[nodiscard]] ActionResult response_error(std::string message);

} // namespace

ActionDispatchResult dispatch_action(DesktopSession& session, std::string_view request)
{
    try {
        auto result = execute_action(session, json::parse(request));
        return {.response = result.response.dump(), .preview_changed = result.preview_changed};
    } catch (const std::exception& error) {
        return {.response = response_error(error.what()).response.dump(), .preview_changed = false};
    }
}

namespace {

[[nodiscard]] ActionResult response_ok(json result, bool preview_changed = false)
{
    return {.response = json{{"ok", true}, {"result", std::move(result)}}, .preview_changed = preview_changed};
}

[[nodiscard]] ActionResult response_error(std::string message)
{
    return {.response = json{{"ok", false}, {"error", std::move(message)}}};
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

[[nodiscard]] ActionResult response_error(const ApplicationError& error)
{
    return {.response = json{
                {"ok", false}, {"error", error.message}, {"code", application_error_code(error.code)}}};
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

[[nodiscard]] json to_json(const ViewportAppearance& preferences)
{
    return json{
        {"guides_visible", preferences.guides_visible},
        {"world_origin_axes_visible", preferences.world_origin_axes_visible},
        {"hdri_backdrop_visible", preferences.hdri_backdrop_visible},
        {"active_hdri_environment_id", preferences.active_hdri_environment_id},
    };
}

[[nodiscard]] json to_json(const AppStateView& state)
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
    };
}

[[nodiscard]] json to_json(const PlantStateView& state)
{
    return json{
        {"plant_age", state.plant_age},
        {"root_physiological_age", state.root_physiological_age},
        {"root_fully_grown_age", state.root_fully_grown_age},
        {"timestep", state.timestep},
        {"paused", state.paused},
        {"root_prototype_id", state.root_prototype_id},
        {"plant_type_id", state.plant_type_id},
        {"module_diagnostic_labels_visible", state.module_diagnostic_labels_visible},
        {"direct_light_bounding_spheres_visible", state.direct_light_bounding_spheres_visible},
        {"accumulated_light_flow_visible", state.accumulated_light_flow_visible},
        {"vigor_flow_visible", state.vigor_flow_visible},
        {"mature_terminal_markers_visible", state.mature_terminal_markers_visible},
        {"direct_light_exposure", state.direct_light_exposure},
        {"accumulated_light", state.accumulated_light},
        {"vigor", state.vigor},
        {"growth_rate", state.growth_rate},
    };
}

[[nodiscard]] json to_json(const PrototypeTreeView& tree)
{
    return json{{"root", prototype_tree_item_to_json(tree.root)}};
}

[[nodiscard]] json to_json(const GrowthSnapshotSummary& summary)
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

[[nodiscard]] ActionResult execute_action(DesktopSession& session, const json& request)
{
    const std::string method = request.at("method").get<std::string>();
    const json params = request.value("params", json::object());

    if (method == "app.get_state") {
        auto state = session.state();
        return state ? response_ok(to_json(*state)) : response_error(state.error());
    }
    if (method == "module.set_active_prototype") {
        if (!params.contains("prototype_id") || !params.at("prototype_id").is_number_unsigned()) {
            return response_error("prototype_id must be an unsigned integer");
        }
        auto result = session.set_active_prototype(params.at("prototype_id").get<std::size_t>());
        return result ? response_ok(json::object(), true) : response_error(result.error());
    }
    if (method == "module.set_active_plant_type") {
        auto result = session.set_active_plant_type(params.at("plant_type_id").get<std::string>());
        return result ? response_ok(json::object(), true) : response_error(result.error());
    }
    if (method == "module.set_age") {
        auto result = session.set_module_physiological_age(params.at("age").get<float>());
        return result ? response_ok(json::object(), true) : response_error(result.error());
    }
    if (method == "module.get_prototype_tree") {
        auto tree = session.prototype_tree();
        return tree ? response_ok(to_json(*tree)) : response_error(tree.error());
    }
    if (method == "module.get_growth_snapshot_summary") {
        auto summary = session.growth_snapshot_summary();
        return summary ? response_ok(to_json(*summary)) : response_error(summary.error());
    }
    if (method == "plant.get_state") {
        auto state = session.plant_state();
        return state ? response_ok(to_json(*state)) : response_error(state.error());
    }
    if (method == "plant.reset") {
        auto result = session.plant_reset();
        return result ? response_ok(json::object(), true) : response_error(result.error());
    }
    if (method == "plant.step") {
        auto result = session.plant_step();
        return result ? response_ok(json::object(), true) : response_error(result.error());
    }
    if (method == "plant.set_timestep") {
        auto result = session.set_plant_timestep(params.at("timestep").get<float>());
        return result ? response_ok(json::object()) : response_error(result.error());
    }
    if (method == "plant.set_diagnostics") {
        auto state = session.plant_state();
        if (!state) {
            return response_error(state.error());
        }
        PlantDiagnosticsUpdate diagnostics{
            .module_diagnostic_labels_visible = params.value(
                "module_diagnostic_labels_visible", state->module_diagnostic_labels_visible),
            .direct_light_bounding_spheres_visible = params.value(
                "direct_light_bounding_spheres_visible", state->direct_light_bounding_spheres_visible),
            .accumulated_light_flow_visible = params.value(
                "accumulated_light_flow_visible", state->accumulated_light_flow_visible),
            .vigor_flow_visible = params.value("vigor_flow_visible", state->vigor_flow_visible),
            .mature_terminal_markers_visible = params.value(
                "mature_terminal_markers_visible", state->mature_terminal_markers_visible),
        };
        auto result = session.update_plant_diagnostics(diagnostics);
        return result ? response_ok(json::object(), true) : response_error(result.error());
    }
    if (method == "workspace.set") {
        auto result = session.set_active_workspace(params.at("workspace").get<std::string>());
        return result ? response_ok(json::object(), true) : response_error(result.error());
    }
    if (method == "plant_types.create") {
        const auto preset_key = params.value("preset_key", std::string("o"));
        if (preset_key.size() != 1) {
            return response_error("preset_key must be a single-letter string");
        }
        auto plant_type = session.create_plant_type(params.at("name").get<std::string>(), preset_key.front());
        return plant_type
                   ? response_ok(json{{"id", plant_type->id}, {"name", plant_type->name}})
                   : response_error(plant_type.error());
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
        return response_ok(json{
            {"preferences", to_json(session.viewport_preferences())},
            {"hdri_environments", environments},
        });
    }
    if (method == "viewport.set_preferences") {
        auto preferences = session.viewport_preferences();
        preferences.guides_visible = params.value("guides_visible", preferences.guides_visible);
        preferences.world_origin_axes_visible =
            params.value("world_origin_axes_visible", preferences.world_origin_axes_visible);
        preferences.hdri_backdrop_visible = params.value("hdri_backdrop_visible", preferences.hdri_backdrop_visible);
        preferences.active_hdri_environment_id =
            params.value("active_hdri_environment_id", preferences.active_hdri_environment_id);
        auto result = session.update_viewport_preferences(std::move(preferences));
        return result ? response_ok(json::object(), true) : response_error(result.error());
    }

    return response_error("unknown method " + method);
}

} // namespace

} // namespace toi::desktop
