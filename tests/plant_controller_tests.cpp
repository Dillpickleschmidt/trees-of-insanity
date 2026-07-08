#include "toi/app/application_commands.hpp"
#include "toi/app/application_controller.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <nlohmann/json.hpp>
#include <string>
#include <utility>

namespace {

using nlohmann::json;

std::filesystem::path fresh_project_path(const std::string& name)
{
    const auto root = std::filesystem::temp_directory_path() / "trees-of-insanity-tests";
    std::filesystem::create_directories(root);
    auto path = root / (name + ".toi.project.json");
    std::filesystem::remove(path);
    return path;
}

toi::app::ApplicationController make_controller(const std::string& name)
{
    toi::app::ApplicationControllerOptions options;
    options.project_path = fresh_project_path(name);
    options.prototype_asset_path = TOI_TEST_PROTOTYPE_ASSET_PATH;
    auto controller = toi::app::ApplicationController::create(std::move(options));
    REQUIRE(controller.has_value());
    return std::move(*controller);
}

} // namespace

TEST_CASE("plant workspace is unlocked and switchable")
{
    auto controller = make_controller("plant-workspace");
    const auto initial = controller.state();
    REQUIRE(initial.has_value());

    bool plant_implemented = false;
    for (const auto& preview : initial->workspace_previews) {
        if (preview.workspace == "plant") {
            plant_implemented = preview.implemented;
        }
    }
    REQUIRE(plant_implemented);

    REQUIRE(controller.set_active_workspace("plant").has_value());
    const auto after = controller.state();
    REQUIRE(after.has_value());
    REQUIRE(after->active_workspace == "plant");
    REQUIRE_FALSE(controller.set_active_workspace("bogus").has_value());
}

TEST_CASE("plant age drives the plant growth summary and clamps to max")
{
    auto controller = make_controller("plant-age");
    const auto state = controller.state();
    REQUIRE(state.has_value());
    const float max_age = state->plant_fully_grown_age;
    REQUIRE(max_age > 0.0F);

    REQUIRE(controller.set_plant_physiological_age(max_age).has_value());
    const auto summary = controller.plant_growth_summary();
    REQUIRE(summary.has_value());
    REQUIRE(summary->module_count >= 1);
    REQUIRE(summary->plant_fully_grown_age == max_age);

    // Over-max requests clamp to the plant's max age.
    REQUIRE(controller.set_plant_physiological_age(max_age * 10.0F).has_value());
    const auto clamped = controller.state();
    REQUIRE(clamped.has_value());
    REQUIRE(clamped->plant_physiological_age <= max_age + 1.0e-3F);
}

TEST_CASE("active preview dispatches to the plant projection in the plant workspace")
{
    auto controller = make_controller("plant-preview");
    const auto state = controller.state();
    REQUIRE(state.has_value());
    REQUIRE(controller.set_plant_physiological_age(state->plant_fully_grown_age).has_value());

    REQUIRE(controller.set_active_workspace("plant").has_value());
    const auto plant_stage = controller.active_preview_stage_projection();
    REQUIRE(plant_stage.has_value());
    REQUIRE(plant_stage->mesh.mesh_count > 0);
    REQUIRE(plant_stage->usd_stage.text.find("def Mesh") != std::string::npos);

    REQUIRE(controller.set_active_workspace("module").has_value());
    const auto module_stage = controller.active_preview_stage_projection();
    REQUIRE(module_stage.has_value());
}

TEST_CASE("plant commands round-trip through the command seam")
{
    auto controller = make_controller("plant-commands");

    const auto set_workspace = toi::app::handle_application_command(
        controller, json{{"id", 1}, {"method", "workspace.set"}, {"params", {{"workspace", "plant"}}}});
    REQUIRE(set_workspace["ok"] == true);

    const auto set_age = toi::app::handle_application_command(
        controller, json{{"id", 2}, {"method", "plant.set_age"}, {"params", {{"age", 60.0}}}});
    REQUIRE(set_age["ok"] == true);

    const auto summary = toi::app::handle_application_command(
        controller, json{{"id", 3}, {"method", "plant.get_growth_summary"}, {"params", json::object()}});
    REQUIRE(summary["ok"] == true);
    REQUIRE(summary["result"].contains("module_count"));

    REQUIRE(toi::app::application_command_changes_preview("plant.set_age"));
    REQUIRE(toi::app::application_command_changes_preview("workspace.set"));

    // Transient preview of a built-in species preset (species gallery), no state change.
    const auto preset_preview = toi::app::handle_application_command(
        controller, json{{"id", 4}, {"method", "plant.preview_preset"}, {"params", {{"preset_key", "i"}, {"age", 200.0}}}});
    REQUIRE(preset_preview["ok"] == true);
    REQUIRE(preset_preview["result"]["mesh_count"].get<int>() > 0);
}
