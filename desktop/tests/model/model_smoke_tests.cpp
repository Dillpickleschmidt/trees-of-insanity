#include "toi/model/desktop_session.hpp"
#include "toi/import/obj_importer.hpp"
#include "toi/render/render_projection.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
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

TEST_CASE("application session opens default module workspace")
{
    const auto project_path = fresh_project_path("session-default-state");
    auto session = toi::model::DesktopSession::create({
        .project_path = project_path,
        .asset_root_path = prototype_path().parent_path().parent_path(),
        .prototype_asset_path = prototype_path(),
    });
    REQUIRE(session.has_value());

    auto state = session->state();
    REQUIRE(state.has_value());
    CHECK(state->active_workspace == "module");
    CHECK(state->active_prototype_id == 8);
    CHECK(state->active_plant_type_id == "plant-type-1");
    CHECK(state->module_physiological_age == state->fully_grown_age);

    bool plant_is_disabled = false;
    for (const auto& preview : state->workspace_previews) {
        if (preview.workspace == "plant") {
            plant_is_disabled = !preview.implemented;
        }
    }
    CHECK(plant_is_disabled);
    CHECK_FALSE(session->set_active_workspace("plant").has_value());
}

TEST_CASE("age scrubbing keeps the growth-preview stage topology stable")
{
    auto session = toi::model::DesktopSession::create({
        .project_path = fresh_project_path("age-scrub-stage-stability"),
        .asset_root_path = prototype_path().parent_path().parent_path(),
        .prototype_asset_path = prototype_path(),
    });
    REQUIRE(session.has_value());

    auto state = session->state();
    REQUIRE(state.has_value());

    REQUIRE(session->set_module_physiological_age(0.0F).has_value());
    auto young_snapshot = session->module_preview_snapshot();
    REQUIRE(young_snapshot.has_value());
    const auto young = toi::render::make_growth_preview_stage_projection(
        young_snapshot->snapshot, young_snapshot->camera_snapshot, young_snapshot->prepared_prototype);

    REQUIRE(session->set_module_physiological_age(state->fully_grown_age).has_value());
    auto mature_snapshot = session->module_preview_snapshot();
    REQUIRE(mature_snapshot.has_value());
    const auto mature = toi::render::make_growth_preview_stage_projection(
        mature_snapshot->snapshot, mature_snapshot->camera_snapshot, mature_snapshot->prepared_prototype);

    // The USD stage text (topology, lights, camera product) is identical across
    // ages; only the mesh point attributes change. That lets the renderer skip a
    // stage reload on age scrub — the "no blink" property.
    CHECK(young.usd_stage.text == mature.usd_stage.text);
    // The visible geometry still changes with age.
    CHECK(young.mesh.vertex_count != mature.mesh.vertex_count);
}
