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
    CHECK(document.contains("module_workspace"));
    CHECK(document.contains("plant_workspace"));
    CHECK(document.contains("ecosystem_workspace"));
}

TEST_CASE("typed Project round-trips independent workspaces")
{
    using namespace toi::project;
    const auto project_path = fresh_project_path("typed-project-round-trip");
    auto project = make_default_project(8, "hdri:module.exr");
    auto second_type = create_plant_type_from_preset("plant-type-2", "Second", 'a');
    REQUIRE(second_type);
    project.plant_type_library.plant_types.push_back(*second_type);
    project.active_workspace = Workspace::Ecosystem;
    project.module_workspace.plant_type_id = "plant-type-2";
    project.module_workspace.physiological_age = 12.5F;
    project.module_workspace.viewport.orbit = {
        .target = {.x = 1.0F, .y = 2.0F, .z = 3.0F},
        .radius = 4.0F,
        .azimuth_radians = 0.5F,
        .elevation_radians = -0.25F,
    };
    project.plant_workspace.plant_type_id = "plant-type-2";
    project.plant_workspace.simulation_timestep = 0.25F;
    project.plant_workspace.diagnostics.vigor_flow_visible = true;
    project.plant_workspace.viewport.active_hdri_environment_id = "hdri:plant.exr";
    project.ecosystem_workspace.viewport.active_hdri_environment_id = "hdri:ecosystem.exr";
    project.ecosystem_workspace.viewport.orbit.radius = 9.0F;
    REQUIRE(save_project(project_path, project));

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

TEST_CASE("Project loading rejects invalid serialized fields")
{
    const auto project_path = fresh_project_path("strict-project-loading");
    auto project = toi::project::make_default_project(8, "hdri:test.exr");
    REQUIRE(toi::project::save_project(project_path, project));

    nlohmann::json document;
    std::ifstream(project_path) >> document;
    auto incomplete = document;
    incomplete.at("plant_workspace").erase("diagnostics");
    std::ofstream(project_path) << incomplete.dump(2) << '\n';
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

    bool plant_is_enabled = false;
    for (const auto& preview : state->workspace_previews) {
        if (preview.workspace == "plant") {
            plant_is_enabled = preview.implemented;
        }
    }
    CHECK(plant_is_enabled);
    CHECK(session->set_active_workspace("plant").has_value());
    CHECK_FALSE(session->set_active_workspace("ecosystem").has_value());
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
        viewport.guides_visible = false;
        viewport.world_origin_axes_visible = false;
        viewport.hdri_backdrop_visible = false;
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
    REQUIRE(reopened->module_preview_snapshot());

    auto project = toi::project::load_project(project_path);
    REQUIRE(project);
    CHECK(project->module_workspace.viewport.orbit.radius == Catch::Approx(1.0F));
    CHECK(project->plant_workspace.plant_type_id == "plant-type-1");
    CHECK(project->plant_workspace.viewport.guides_visible);
    CHECK(project->ecosystem_workspace.viewport.guides_visible);
}

TEST_CASE("project-only command save failure preserves session state")
{
    const auto project_path = fresh_project_path("project-only-save-failure");
    auto session = toi::model::DesktopSession::create(session_options(project_path));
    REQUIRE(session);
    const auto before = session->state();
    REQUIRE(before);

    auto blocked_temporary_path = project_path;
    blocked_temporary_path += ".tmp";
    REQUIRE(std::filesystem::create_directory(blocked_temporary_path));
    CHECK_FALSE(session->set_module_physiological_age(1.0F));

    const auto after = session->state();
    REQUIRE(after);
    CHECK(after->module_physiological_age == Catch::Approx(before->module_physiological_age));
    const auto persisted = toi::project::load_project(project_path);
    REQUIRE(persisted);
    CHECK(persisted->module_workspace.physiological_age == Catch::Approx(before->module_physiological_age));
    std::filesystem::remove_all(blocked_temporary_path);
}

TEST_CASE("coupled command save failure preserves project simulation and camera state")
{
    const auto project_path = fresh_project_path("coupled-save-failure");
    auto session = toi::model::DesktopSession::create(session_options(project_path));
    REQUIRE(session);
    REQUIRE(session->set_active_workspace("plant"));
    REQUIRE(session->update_active_orbit({.radius = 2.0F}));
    REQUIRE(session->plant_step());

    const auto before_state = session->plant_state();
    REQUIRE(before_state);
    CHECK(before_state->plant_age > 0.0F);
    CHECK_FALSE(session->active_camera_needs_frame());
    auto updated_type = session->plant_type(before_state->plant_type_id);
    REQUIRE(updated_type);
    const float original_growth_rate = updated_type->parameters.plant_growth_rate;
    updated_type->parameters.plant_growth_rate *= 0.5F;

    auto blocked_temporary_path = project_path;
    blocked_temporary_path += ".tmp";
    REQUIRE(std::filesystem::create_directory(blocked_temporary_path));
    CHECK_FALSE(session->update_plant_type(*updated_type));

    const auto after_state = session->plant_state();
    REQUIRE(after_state);
    CHECK(after_state->plant_age == Catch::Approx(before_state->plant_age));
    CHECK_FALSE(session->active_camera_needs_frame());
    const auto after_type = session->plant_type(before_state->plant_type_id);
    REQUIRE(after_type);
    CHECK(after_type->parameters.plant_growth_rate == Catch::Approx(original_growth_rate));
    const auto persisted = toi::project::load_project(project_path);
    REQUIRE(persisted);
    const auto* persisted_type = toi::project::plant_type_by_id(*persisted, before_state->plant_type_id);
    REQUIRE(persisted_type != nullptr);
    CHECK(persisted_type->parameters.plant_growth_rate == Catch::Approx(original_growth_rate));
    std::filesystem::remove_all(blocked_temporary_path);
}

TEST_CASE("session rejects missing prototype assets")
{
    using namespace toi::project;
    const auto hdri = "hdri:meadow_2_4k.exr";

    const auto missing_path = fresh_project_path("missing-prototype-project");
    auto missing = make_default_project(8, hdri);
    missing.module_workspace.prototype_id = 999;
    REQUIRE(save_project(missing_path, missing));
    CHECK_FALSE(toi::model::DesktopSession::create(session_options(missing_path)));

    const auto missing_root_path = fresh_project_path("missing-root-prototype-project");
    auto missing_root = make_default_project(8, hdri);
    missing_root.plant_workspace.root_prototype_id = 999;
    REQUIRE(save_project(missing_root_path, missing_root));
    CHECK_FALSE(toi::model::DesktopSession::create(session_options(missing_root_path)));
}

TEST_CASE("Plant workspace steps and resets one diagnosed root")
{
    const auto project_path = fresh_project_path("root-only-plant-workspace");
    auto session = toi::model::DesktopSession::create(session_options(project_path));
    REQUIRE(session);
    REQUIRE(session->active_camera_needs_frame());
    REQUIRE(session->update_active_orbit({
        .target = {.x = 1.0F, .y = 2.0F, .z = 3.0F},
        .radius = 4.0F,
        .azimuth_radians = 0.5F,
        .elevation_radians = 0.25F,
    }));
    REQUIRE(session->set_active_workspace("plant"));
    REQUIRE(session->active_camera_needs_frame());
    REQUIRE(session->update_active_orbit({
        .target = {.x = 0.0F, .y = 0.0F, .z = 0.5F},
        .radius = 2.0F,
        .azimuth_radians = -0.5F,
        .elevation_radians = 0.1F,
    }));

    auto initial = session->plant_state();
    REQUIRE(initial);
    CHECK(initial->paused);
    CHECK(initial->plant_age == Catch::Approx(0.0F));
    CHECK(initial->root_physiological_age == Catch::Approx(0.0F));
    CHECK(initial->direct_light_exposure == Catch::Approx(1.0F));
    CHECK(initial->accumulated_light == Catch::Approx(1.0F));
    CHECK(initial->vigor == Catch::Approx(1.0F));

    auto initial_preview = session->plant_preview_snapshot();
    REQUIRE(initial_preview);
    REQUIRE(initial_preview->snapshot.modules.size() == 1);
    const auto root_range = initial_preview->snapshot.modules.front().segments;
    CHECK(std::ranges::all_of(
        initial_preview->snapshot.segments.subspan(root_range.offset, root_range.count),
        [](const auto& segment) { return toi::growth::distance(segment.parent_position, segment.child_position) <= toi::growth::kEpsilon; }));
    const auto seed_projection = toi::render::make_plant_preview_stage_projection(
        initial_preview->snapshot, initial_preview->mature_root_snapshot,
        {.show_collision_spheres = true, .show_labels = true});
    CHECK(seed_projection.diagnostic_lines.empty());
    CHECK(seed_projection.diagnostic_spheres.empty());
    REQUIRE(seed_projection.diagnostic_labels.size() == 1);

    REQUIRE(session->set_plant_timestep(2.0F));
    REQUIRE(session->plant_step());
    CHECK_FALSE(session->active_camera_needs_frame());
    CHECK(session->active_orbit().target.z == Catch::Approx(0.5F));
    CHECK(session->active_orbit().radius == Catch::Approx(2.0F));
    CHECK(session->active_orbit().azimuth_radians == Catch::Approx(-0.5F));
    CHECK(session->active_orbit().elevation_radians == Catch::Approx(0.1F));
    auto stepped = session->plant_state();
    REQUIRE(stepped);
    CHECK(stepped->plant_age == Catch::Approx(2.0F));
    CHECK(stepped->root_physiological_age == Catch::Approx(initial->growth_rate * 2.0F));
    auto developed_preview = session->plant_preview_snapshot();
    REQUIRE(developed_preview);
    const auto developed_projection = toi::render::make_plant_preview_stage_projection(
        developed_preview->snapshot, developed_preview->mature_root_snapshot,
        {.show_collision_spheres = true, .show_labels = true});
    REQUIRE(developed_projection.diagnostic_spheres.size() == 1);
    const auto& projected_sphere = developed_projection.diagnostic_spheres.front();
    const auto& snapshot_sphere = developed_preview->snapshot.modules.front().collision_sphere;
    CHECK(projected_sphere.center.x == Catch::Approx(snapshot_sphere.center.x));
    CHECK(projected_sphere.center.y == Catch::Approx(snapshot_sphere.center.y));
    CHECK(projected_sphere.center.z == Catch::Approx(snapshot_sphere.center.z));
    CHECK(projected_sphere.radius == Catch::Approx(snapshot_sphere.radius));
    CHECK(projected_sphere.color.x == Catch::Approx(0.337F));
    CHECK(projected_sphere.color.y == Catch::Approx(0.706F));
    CHECK(projected_sphere.color.z == Catch::Approx(0.914F));
    CHECK(projected_sphere.alpha > 0.0F);
    CHECK(projected_sphere.alpha < 1.0F);
    CHECK(developed_projection.camera.eye.x == Catch::Approx(seed_projection.camera.eye.x));
    CHECK(developed_projection.camera.eye.y == Catch::Approx(seed_projection.camera.eye.y));
    CHECK(developed_projection.camera.eye.z == Catch::Approx(seed_projection.camera.eye.z));

    REQUIRE(session->update_plant_diagnostics({
        .module_diagnostic_labels_visible = true,
        .direct_light_bounding_spheres_visible = true,
    }));
    CHECK(session->plant_state()->plant_age == Catch::Approx(2.0F));
    REQUIRE(session->plant_reset());
    CHECK(session->active_camera_needs_frame());
    CHECK(session->plant_state()->plant_age == Catch::Approx(0.0F));
    CHECK(session->plant_state()->root_physiological_age == Catch::Approx(0.0F));

    auto reopened = toi::model::DesktopSession::create(session_options(project_path));
    REQUIRE(reopened);
    auto reopened_state = reopened->plant_state();
    REQUIRE(reopened_state);
    CHECK(reopened_state->plant_age == Catch::Approx(0.0F));
    CHECK(reopened_state->timestep == Catch::Approx(2.0F));
    CHECK(reopened_state->module_diagnostic_labels_visible);
    CHECK(reopened_state->direct_light_bounding_spheres_visible);
    REQUIRE(reopened->set_active_workspace("module"));
    CHECK(reopened->active_orbit().radius == Catch::Approx(4.0F));
}

TEST_CASE("Plant maturity crossing exposes one attached generation")
{
    auto session = toi::model::DesktopSession::create(
        session_options(fresh_project_path("first-attached-generation")));
    REQUIRE(session);
    REQUIRE(session->set_active_workspace("plant"));
    REQUIRE(session->update_active_orbit({.radius = 3.0F}));
    REQUIRE(session->set_plant_timestep(1'000.0F));
    REQUIRE(session->update_plant_diagnostics({
        .module_diagnostic_labels_visible = true,
        .direct_light_bounding_spheres_visible = true,
        .accumulated_light_flow_visible = true,
        .vigor_flow_visible = true,
        .mature_terminal_markers_visible = true,
    }));
    REQUIRE(session->plant_step());

    auto attached = session->plant_preview_snapshot();
    REQUIRE(attached);
    REQUIRE(attached->snapshot.modules.size() > 1);
    CHECK(attached->snapshot.attachment_events.size() == attached->snapshot.modules.size() - 1);
    for (std::size_t index = 1; index < attached->snapshot.modules.size(); ++index) {
        CHECK(attached->snapshot.modules[index].parent_module_id == 0);
        CHECK(attached->snapshot.modules[index].physiological_age == Catch::Approx(0.0F));
    }
    CHECK(session->active_orbit().radius == Catch::Approx(3.0F));
    CHECK_FALSE(session->active_camera_needs_frame());

    const auto module_count = attached->snapshot.modules.size();
    REQUIRE(session->plant_step());
    auto flowing = session->plant_preview_snapshot();
    REQUIRE(flowing);
    CHECK(flowing->snapshot.modules.size() == module_count);
    CHECK_FALSE(flowing->snapshot.flow_diagnostics.empty());
    const auto projection = toi::render::make_plant_preview_stage_projection(
        flowing->snapshot, flowing->mature_root_snapshot,
        {.show_collision_spheres = true,
         .show_labels = true,
         .show_accumulated_light_flow = true,
         .show_vigor_flow = true,
         .show_mature_terminals = true});
    CHECK(projection.diagnostic_labels.size() == module_count);
    REQUIRE_FALSE(projection.diagnostic_surface_vertices.empty());
    CHECK(projection.diagnostic_surface_vertices.size() % 6 == 0);
    CHECK(std::ranges::any_of(projection.diagnostic_surface_vertices, [](const auto& vertex) {
        return vertex.animation_direction < 0.0F;
    }));
    CHECK(std::ranges::any_of(projection.diagnostic_surface_vertices, [](const auto& vertex) {
        return vertex.animation_direction > 0.0F;
    }));
    for (const auto& vertex : projection.diagnostic_surface_vertices) {
        CHECK(std::ranges::any_of(projection.mesh_attributes, [&](const auto& mesh) {
            return std::ranges::any_of(mesh.points, [&](const auto& point) {
                return point.x == vertex.position.x && point.y == vertex.position.y && point.z == vertex.position.z;
            });
        }));
    }
    const auto light_only_projection = toi::render::make_plant_preview_stage_projection(
        flowing->snapshot, flowing->mature_root_snapshot, {.show_accumulated_light_flow = true});
    REQUIRE_FALSE(light_only_projection.diagnostic_surface_vertices.empty());
    CHECK(std::ranges::all_of(light_only_projection.diagnostic_surface_vertices, [](const auto& vertex) {
        return vertex.animation_direction < 0.0F;
    }));
    const auto vigor_only_projection = toi::render::make_plant_preview_stage_projection(
        flowing->snapshot, flowing->mature_root_snapshot, {.show_vigor_flow = true});
    REQUIRE_FALSE(vigor_only_projection.diagnostic_surface_vertices.empty());
    CHECK(std::ranges::all_of(vigor_only_projection.diagnostic_surface_vertices, [](const auto& vertex) {
        return vertex.animation_direction > 0.0F;
    }));
    CHECK(projection.diagnostic_surface_vertices.size() ==
          light_only_projection.diagnostic_surface_vertices.size() +
              vigor_only_projection.diagnostic_surface_vertices.size());
    const auto hidden_flow_projection = toi::render::make_plant_preview_stage_projection(
        flowing->snapshot, flowing->mature_root_snapshot);
    CHECK(hidden_flow_projection.diagnostic_surface_vertices.empty());
    std::vector<bool> continuation_targets(flowing->snapshot.segments.size(), false);
    for (const auto& segment : flowing->snapshot.segments) {
        if (segment.main_continuation_segment) continuation_targets[*segment.main_continuation_segment] = true;
    }
    CHECK(projection.mesh.chain_count ==
          static_cast<std::size_t>(std::ranges::count(continuation_targets, false)));
    REQUIRE(projection.diagnostic_spheres.size() == module_count);
    CHECK((projection.diagnostic_spheres[0].color.x != projection.diagnostic_spheres[1].color.x ||
           projection.diagnostic_spheres[0].color.y != projection.diagnostic_spheres[1].color.y ||
           projection.diagnostic_spheres[0].color.z != projection.diagnostic_spheres[1].color.z));
    CHECK_FALSE(projection.diagnostic_lines.empty());
}

TEST_CASE("Plant-selected type changes reset the transient simulation")
{
    auto session = toi::model::DesktopSession::create(
        session_options(fresh_project_path("plant-type-reset")));
    REQUIRE(session);
    REQUIRE(session->plant_step());
    CHECK(session->plant_state()->plant_age > 0.0F);

    auto selected = session->plant_type("plant-type-1");
    REQUIRE(selected);
    selected->parameters.plant_growth_rate *= 0.5F;
    REQUIRE(session->update_plant_type(*selected));
    CHECK(session->plant_state()->plant_age == Catch::Approx(0.0F));
    CHECK(session->active_camera_needs_frame());

    auto second = session->create_plant_type("Second", 'a');
    REQUIRE(second);
    REQUIRE(session->plant_step());
    REQUIRE(session->delete_plant_type("plant-type-1"));
    CHECK(session->plant_state()->plant_age == Catch::Approx(0.0F));
    CHECK(session->plant_state()->plant_type_id == second->id);
}

TEST_CASE("growth projection follows the prepared main continuation")
{
    toi::growth::BranchModulePrototype prepared;
    prepared.nodes.resize(4);
    prepared.segments = {
        {.parent_node = 0, .child_node = 1, .direction = {.z = 1.0F}},
        {.parent_node = 1, .child_node = 2, .direction = {.z = 1.0F}},
        {.parent_node = 1, .child_node = 3, .direction = {.x = 0.1F, .z = 0.995F}},
    };
    prepared.root_node = 0;
    prepared.terminal_nodes = {2, 3};
    prepared.child_segments_by_node = {{0}, {1, 2}, {}, {}};
    prepared.incoming_segment_by_node = {std::nullopt, 0, 1, 2};
    prepared.main_child_segment_by_node = {std::nullopt, 2, std::nullopt, std::nullopt};
    prepared.main_axis_terminal_node = 3;

    const toi::growth::Vec3 selected_terminal{.x = 1.0F, .z = 2.0F};
    const toi::growth::GrowthSnapshot snapshot{
        .segments = {
            {.source_segment_id = 0,
             .parent_position = {},
             .child_position = {.z = 1.0F},
             .diameter = 0.2F},
            {.source_segment_id = 1,
             .parent_position = {.z = 1.0F},
             .child_position = {.z = 2.0F},
             .diameter = 0.1F},
            {.source_segment_id = 2,
             .parent_position = {.z = 1.0F},
             .child_position = selected_terminal,
             .diameter = 0.1F},
        },
    };

    const auto projection = toi::render::make_growth_preview_stage_projection(snapshot, snapshot, prepared);

    REQUIRE(projection.mesh_attributes.size() == 2);
    CHECK(std::ranges::any_of(projection.mesh_attributes.front().points, [&](const auto point) {
        return point.x == selected_terminal.x && point.y == selected_terminal.y && point.z == selected_terminal.z;
    }));
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
