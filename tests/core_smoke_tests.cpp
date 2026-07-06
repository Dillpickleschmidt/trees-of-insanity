#include "toi/app/application_controller.hpp"
#include "toi/import/obj_importer.hpp"
#include "toi/native/native_core.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>

namespace {

std::filesystem::path prototype_path()
{
    return TOI_TEST_PROTOTYPE_ASSET_PATH;
}

std::filesystem::path fresh_project_path(std::string_view name)
{
    const auto root = std::filesystem::temp_directory_path() / "trees-of-insanity-tests";
    std::filesystem::create_directories(root);
    auto path = root / (std::string(name) + ".toi.project.json");
    std::filesystem::remove(path);
    return path;
}

std::string take_string(char* value)
{
    REQUIRE(value != nullptr);
    std::string result(value);
    toi_free_string(value);
    return result;
}

} // namespace

TEST_CASE("bundled OBJ imports branch module prototypes")
{
    auto library = toi::import::load_branch_module_prototype_library_from_obj(prototype_path());
    REQUIRE(library.has_value());
    REQUIRE(library->prototypes.size() == 9);

    const auto cube_008 = toi::import::prototype_id_by_name(*library, "Cube.008");
    REQUIRE(cube_008.has_value());
    CHECK(*cube_008 == 8);
    CHECK(library->prototypes[*cube_008].segments.size() == 25);
}

TEST_CASE("application controller opens default module workspace")
{
    const auto project_path = fresh_project_path("controller-default-state");
    auto controller = toi::app::ApplicationController::create({
        .project_path = project_path,
        .asset_root_path = prototype_path().parent_path().parent_path(),
        .prototype_asset_path = prototype_path(),
    });
    REQUIRE(controller.has_value());

    auto state = controller->state();
    REQUIRE(state.has_value());
    CHECK(state->active_workspace == "module");
    CHECK(state->active_prototype_id == 8);
    CHECK(state->active_plant_type_id == "plant-type-1");
    CHECK(state->module_physiological_age == state->fully_grown_age);
}

TEST_CASE("age scrubbing keeps the growth-preview stage topology stable")
{
    auto controller = toi::app::ApplicationController::create({
        .project_path = fresh_project_path("age-scrub-stage-stability"),
        .asset_root_path = prototype_path().parent_path().parent_path(),
        .prototype_asset_path = prototype_path(),
    });
    REQUIRE(controller.has_value());

    auto state = controller->state();
    REQUIRE(state.has_value());

    REQUIRE(controller->set_module_physiological_age(0.0F).has_value());
    auto young = controller->growth_preview_stage_projection();
    REQUIRE(young.has_value());

    REQUIRE(controller->set_module_physiological_age(state->fully_grown_age).has_value());
    auto mature = controller->growth_preview_stage_projection();
    REQUIRE(mature.has_value());

    // The USD stage text (topology, lights, camera product) is identical across
    // ages; only the mesh point attributes change. That lets the renderer skip a
    // stage reload on age scrub — the "no blink" property.
    CHECK(young->usd_stage.text == mature->usd_stage.text);
    // The visible geometry still changes with age.
    CHECK(young->mesh.vertex_count != mature->mesh.vertex_count);
}

TEST_CASE("C ABI creates core and handles app.get_state")
{
    const auto project_path = fresh_project_path("c-abi-app-state");
    const auto options = nlohmann::json{
        {"project_path", project_path.string()},
        {"asset_root_path", prototype_path().parent_path().parent_path().string()},
        {"prototype_asset_path", prototype_path().string()},
    };

    ToiNativeCore* core = toi_create(options.dump().c_str());
    INFO(take_string(toi_last_error_json()));
    REQUIRE(core != nullptr);

    const auto request = nlohmann::json{{"id", 1}, {"method", "app.get_state"}, {"params", nlohmann::json::object()}};
    const auto response = nlohmann::json::parse(take_string(toi_handle_command(core, request.dump().c_str())));
    toi_destroy(core);

    REQUIRE(response.at("ok").get<bool>());
    CHECK(response.at("result").at("active_prototype_id").get<std::size_t>() == 8);
    CHECK(response.at("result").at("active_workspace").get<std::string>() == "module");
}
