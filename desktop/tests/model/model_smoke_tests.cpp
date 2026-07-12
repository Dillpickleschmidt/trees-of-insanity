#include "toi/model/desktop_session.hpp"
#include "toi/import/obj_importer.hpp"
#include "toi/project/project.hpp"
#include "toi/render/render_projection.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
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
    std::filesystem::remove(path.string() + ".tmp");
    return path;
}

toi::model::DesktopSessionOptions session_options(const std::filesystem::path& project_path)
{
    return {
        .project_path = project_path,
        .asset_root_path = prototype_path().parent_path().parent_path(),
        .prototype_asset_path = prototype_path(),
    };
}

std::string file_contents(const std::filesystem::path& path)
{
    std::ifstream input(path);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
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

TEST_CASE("fresh project contains complete typed workspace state")
{
    const auto project_path = fresh_project_path("fresh-typed-workspaces");
    auto session = toi::model::DesktopSession::create(session_options(project_path));
    REQUIRE(session);

    auto project = toi::project::load_project(project_path);
    REQUIRE(project);
    CHECK(project->version == 2);
    CHECK(project->active_workspace == toi::project::Workspace::Module);
    CHECK(project->plant_type_library.plant_types.size() == 1);
    CHECK(project->module_workspace.prototype_id == 8);
    CHECK(project->module_workspace.plant_type_id == "plant-type-1");
    CHECK(project->module_workspace.physiological_age == Catch::Approx(63.5755F).margin(0.001F));
    CHECK(project->plant_workspace.root_prototype_id == 8);
    CHECK(project->plant_workspace.plant_type_id == "plant-type-1");
    CHECK(project->plant_workspace.simulation_timestep == Catch::Approx(1.0F));
    CHECK_FALSE(project->plant_workspace.diagnostics.module_diagnostic_labels_visible);
    CHECK_FALSE(project->plant_workspace.diagnostics.direct_light_bounding_spheres_visible);
    CHECK_FALSE(project->plant_workspace.diagnostics.accumulated_light_flow_visible);
    CHECK_FALSE(project->plant_workspace.diagnostics.vigor_flow_visible);
    CHECK_FALSE(project->plant_workspace.diagnostics.mature_terminal_markers_visible);
    CHECK(project->module_workspace.viewport.active_hdri_environment_id == "hdri:meadow_2_4k.exr");
    CHECK(project->plant_workspace.viewport.orbit.radius == Catch::Approx(1.0F));
    CHECK(project->ecosystem_workspace.viewport.orbit.radius == Catch::Approx(1.0F));

    nlohmann::json document;
    std::ifstream(project_path) >> document;
    CHECK(document.at("plant_type_library").contains("plant_types"));
    CHECK_FALSE(document.at("plant_type_library").contains("active_plant_type_id"));
    CHECK(document.contains("module_workspace"));
    CHECK(document.contains("plant_workspace"));
    CHECK(document.contains("ecosystem_workspace"));
}

TEST_CASE("typed Project round-trips independent workspaces")
{
    using namespace toi::project;
    const auto project_path = fresh_project_path("typed-project-round-trip");
    auto project = make_default_project(8, "hdri:module.exr");
    REQUIRE(project);
    auto second_type = create_plant_type_from_preset("plant-type-2", "Second", 'a');
    REQUIRE(second_type);
    project->plant_type_library.plant_types.push_back(*second_type);
    project->active_workspace = Workspace::Ecosystem;
    project->module_workspace.plant_type_id = "plant-type-2";
    project->module_workspace.physiological_age = 12.5F;
    project->module_workspace.viewport.orbit = {
        .target = {.x = 1.0F, .y = 2.0F, .z = 3.0F},
        .radius = 4.0F,
        .azimuth_radians = 0.5F,
        .elevation_radians = -0.25F,
    };
    project->plant_workspace.plant_type_id = "plant-type-2";
    project->plant_workspace.simulation_timestep = 0.25F;
    project->plant_workspace.diagnostics.vigor_flow_visible = true;
    project->plant_workspace.viewport.active_hdri_environment_id = "hdri:plant.exr";
    project->ecosystem_workspace.viewport.active_hdri_environment_id = "hdri:ecosystem.exr";
    project->ecosystem_workspace.viewport.orbit.radius = 9.0F;
    REQUIRE(save_project(project_path, *project));

    auto loaded = load_project(project_path);
    REQUIRE(loaded);
    CHECK(loaded->active_workspace == Workspace::Ecosystem);
    CHECK(loaded->module_workspace.viewport.orbit.target.y == Catch::Approx(2.0F));
    CHECK(loaded->plant_workspace.simulation_timestep == Catch::Approx(0.25F));
    CHECK(loaded->plant_workspace.diagnostics.vigor_flow_visible);
    CHECK(loaded->plant_workspace.viewport.active_hdri_environment_id == "hdri:plant.exr");
    CHECK(loaded->ecosystem_workspace.viewport.active_hdri_environment_id == "hdri:ecosystem.exr");
    CHECK(loaded->ecosystem_workspace.viewport.orbit.radius == Catch::Approx(9.0F));

    REQUIRE(delete_plant_type(*loaded, "plant-type-2"));
    CHECK(loaded->module_workspace.plant_type_id == "plant-type-1");
    CHECK(loaded->plant_workspace.plant_type_id == "plant-type-1");
}

TEST_CASE("Project loading rejects incomplete and dangling workspace state")
{
    const auto project_path = fresh_project_path("strict-project-loading");
    auto project = toi::project::make_default_project(8, "hdri:test.exr");
    REQUIRE(project);
    REQUIRE(toi::project::save_project(project_path, *project));

    nlohmann::json document;
    std::ifstream(project_path) >> document;
    auto incomplete = document;
    incomplete.at("plant_workspace").erase("diagnostics");
    std::ofstream(project_path) << incomplete.dump(2) << '\n';
    CHECK_FALSE(toi::project::load_project(project_path));

    auto dangling = document;
    dangling.at("module_workspace").at("plant_type_id") = "missing";
    std::ofstream(project_path) << dangling.dump(2) << '\n';
    CHECK_FALSE(toi::project::load_project(project_path));

    auto unknown_workspace = document;
    unknown_workspace.at("active_workspace") = "forest";
    std::ofstream(project_path) << unknown_workspace.dump(2) << '\n';
    CHECK_FALSE(toi::project::load_project(project_path));
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

TEST_CASE("Module and viewport state persist through session reopen")
{
    const auto project_path = fresh_project_path("session-workspace-persistence");
    {
        auto session = toi::model::DesktopSession::create(session_options(project_path));
        REQUIRE(session);
        auto plant_type = session->create_plant_type("Persistent", 'a');
        REQUIRE(plant_type);
        REQUIRE(session->set_active_prototype(7));
        REQUIRE(session->set_active_plant_type(plant_type->id));
        REQUIRE(session->set_module_physiological_age(10.0F));
        auto viewport = session->viewport_preferences();
        auto invalid_viewport = viewport;
        invalid_viewport.orbit.radius = 0.0F;
        CHECK_FALSE(session->update_viewport_preferences(invalid_viewport));
        CHECK(session->viewport_preferences().orbit.radius == Catch::Approx(1.0F));
        viewport.guides_visible = false;
        viewport.world_origin_axes_visible = false;
        viewport.hdri_backdrop_visible = false;
        viewport.orbit.target = {.x = 1.0F, .y = 2.0F, .z = 3.0F};
        viewport.orbit.radius = 4.0F;
        REQUIRE(session->update_viewport_preferences(viewport));
    }

    auto reopened = toi::model::DesktopSession::create(session_options(project_path));
    REQUIRE(reopened);
    auto state = reopened->state();
    REQUIRE(state);
    CHECK(state->active_prototype_id == 7);
    CHECK(state->active_plant_type_id == "plant-type-2");
    CHECK(state->module_physiological_age == Catch::Approx(10.0F));
    const auto viewport = reopened->viewport_preferences();
    CHECK_FALSE(viewport.guides_visible);
    CHECK_FALSE(viewport.world_origin_axes_visible);
    CHECK_FALSE(viewport.hdri_backdrop_visible);
    CHECK(viewport.orbit.target.z == Catch::Approx(3.0F));
    CHECK(viewport.orbit.radius == Catch::Approx(4.0F));
    REQUIRE(reopened->module_preview_snapshot());

    auto project = toi::project::load_project(project_path);
    REQUIRE(project);
    CHECK(project->plant_workspace.plant_type_id == "plant-type-1");
    CHECK(project->plant_workspace.viewport.guides_visible);
    CHECK(project->ecosystem_workspace.viewport.guides_visible);
}

TEST_CASE("session rejects disabled workspaces and missing asset prototypes without rewriting")
{
    using namespace toi::project;
    const auto hdri = "hdri:meadow_2_4k.exr";

    const auto disabled_path = fresh_project_path("disabled-workspace-project");
    auto disabled = make_default_project(8, hdri);
    REQUIRE(disabled);
    disabled->active_workspace = Workspace::Plant;
    REQUIRE(save_project(disabled_path, *disabled));
    const auto disabled_bytes = file_contents(disabled_path);
    CHECK_FALSE(toi::model::DesktopSession::create(session_options(disabled_path)));
    CHECK(file_contents(disabled_path) == disabled_bytes);

    const auto missing_path = fresh_project_path("missing-prototype-project");
    auto missing = make_default_project(8, hdri);
    REQUIRE(missing);
    missing->module_workspace.prototype_id = 999;
    REQUIRE(save_project(missing_path, *missing));
    const auto missing_bytes = file_contents(missing_path);
    CHECK_FALSE(toi::model::DesktopSession::create(session_options(missing_path)));
    CHECK(file_contents(missing_path) == missing_bytes);

    const auto missing_root_path = fresh_project_path("missing-root-prototype-project");
    auto missing_root = make_default_project(8, hdri);
    REQUIRE(missing_root);
    missing_root->plant_workspace.root_prototype_id = 999;
    REQUIRE(save_project(missing_root_path, *missing_root));
    const auto missing_root_bytes = file_contents(missing_root_path);
    CHECK_FALSE(toi::model::DesktopSession::create(session_options(missing_root_path)));
    CHECK(file_contents(missing_root_path) == missing_root_bytes);

    const auto over_mature_path = fresh_project_path("over-mature-project");
    auto over_mature = make_default_project(8, hdri);
    REQUIRE(over_mature);
    over_mature->module_workspace.physiological_age = 64.0F;
    REQUIRE(save_project(over_mature_path, *over_mature));
    const auto over_mature_bytes = file_contents(over_mature_path);
    CHECK_FALSE(toi::model::DesktopSession::create(session_options(over_mature_path)));
    CHECK(file_contents(over_mature_path) == over_mature_bytes);
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
