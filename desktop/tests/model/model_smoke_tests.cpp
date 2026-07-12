#include "toi/model/desktop_session.hpp"
#include "toi/import/obj_importer.hpp"
#include "toi/render/render_projection.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
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
    auto library = toi::import::load_branch_module_prototype_library_from_obj(prototype_path(), 2.0F);
    REQUIRE(library.has_value());
    REQUIRE(library->prototypes.size() == 9);

    const auto cube_008 = toi::import::prototype_id_by_name(*library, "Cube.008");
    REQUIRE(cube_008.has_value());
    CHECK(*cube_008 == 8);
    const auto& prototype = library->prototypes[*cube_008];
    CHECK(prototype.segments.size() == 25);
    CHECK(prototype.segments.front().max_length == Catch::Approx(0.242014F));
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
    CHECK(state->fully_grown_age == Catch::Approx(63.5755F).margin(0.001F));

    REQUIRE(session->set_module_physiological_age(0.0F).has_value());
    auto young_snapshot = session->module_preview_snapshot();
    REQUIRE(young_snapshot.has_value());
    const auto young = toi::render::make_growth_preview_stage_projection(
        young_snapshot->snapshot, young_snapshot->camera_snapshot, young_snapshot->prepared_prototype);

    REQUIRE(session->set_module_physiological_age(state->fully_grown_age).has_value());
    auto mature_snapshot = session->module_preview_snapshot();
    REQUIRE(mature_snapshot.has_value());
    REQUIRE_FALSE(mature_snapshot->snapshot.segments.empty());
    auto bounds_min = mature_snapshot->snapshot.segments.front().parent_position;
    auto bounds_max = bounds_min;
    float max_diameter = 0.0F;
    const auto include_in_bounds = [&](toi::growth::Vec3 point) {
        bounds_min.x = std::min(bounds_min.x, point.x);
        bounds_min.y = std::min(bounds_min.y, point.y);
        bounds_min.z = std::min(bounds_min.z, point.z);
        bounds_max.x = std::max(bounds_max.x, point.x);
        bounds_max.y = std::max(bounds_max.y, point.y);
        bounds_max.z = std::max(bounds_max.z, point.z);
    };
    for (const auto& segment : mature_snapshot->snapshot.segments) {
        include_in_bounds(segment.parent_position);
        include_in_bounds(segment.child_position);
        max_diameter = std::max(max_diameter, segment.diameter);
    }
    const float mature_extent = toi::growth::distance(bounds_min, bounds_max);
    CHECK(mature_extent == Catch::Approx(1.079209F));
    CHECK(max_diameter == Catch::Approx(0.041231F));
    CHECK(max_diameter / mature_extent == Catch::Approx(0.038205F));

    const auto mature = toi::render::make_growth_preview_stage_projection(
        mature_snapshot->snapshot, mature_snapshot->camera_snapshot, mature_snapshot->prepared_prototype);

    auto enlarged_camera_snapshot = mature_snapshot->camera_snapshot;
    for (auto& segment : enlarged_camera_snapshot.segments) {
        segment.parent_position = toi::growth::scale(segment.parent_position, 100.0F);
        segment.child_position = toi::growth::scale(segment.child_position, 100.0F);
        segment.diameter *= 100.0F;
    }
    const auto enlarged = toi::render::make_growth_preview_stage_projection(
        mature_snapshot->snapshot, enlarged_camera_snapshot, mature_snapshot->prepared_prototype);
    const float camera_distance = toi::growth::distance(mature.camera.eye, mature.camera.target);
    const float enlarged_camera_distance = toi::growth::distance(enlarged.camera.eye, enlarged.camera.target);
    CHECK(enlarged_camera_distance / camera_distance == Catch::Approx(100.0F));

    // The USD stage text (topology, lights, camera product) is identical across
    // ages; only the mesh point attributes change. That lets the renderer skip a
    // stage reload on age scrub — the "no blink" property.
    CHECK(young.usd_stage.text == mature.usd_stage.text);
    CHECK(mature.usd_stage.text.find("metersPerUnit = 1") != std::string::npos);
    // The visible geometry still changes with age.
    CHECK(young.mesh.vertex_count != mature.mesh.vertex_count);
}
