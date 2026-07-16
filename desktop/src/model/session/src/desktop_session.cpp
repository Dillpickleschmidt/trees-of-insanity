#include "toi/model/desktop_session.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace toi::model {
namespace {

constexpr std::string_view kDefaultPrototypeName = "Cube.008";
constexpr std::string_view kHdriIdPrefix = "hdri:";
constexpr std::string_view kDefaultHdriFile = "meadow_2_4k.exr";
constexpr float kPrototypeLibraryGeometryScale = 2.0F;

[[nodiscard]] bool is_hdri_file(const std::filesystem::path& path)
{
    if (!path.has_extension()) {
        return false;
    }
    auto extension = path.extension().string();
    std::ranges::transform(extension, extension.begin(),
                           [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return extension == ".exr" || extension == ".hdr";
}

[[nodiscard]] std::string hdri_id_for(const std::string& file_name)
{
    return std::string(kHdriIdPrefix) + file_name;
}

// The HDRI environment library is the set of HDR images bundled under assets.
[[nodiscard]] std::vector<HdriEnvironment> enumerate_hdri_environments(const std::filesystem::path& asset_root)
{
    std::vector<HdriEnvironment> environments;
    const auto hdri_dir = asset_root / "HDRI";
    std::error_code error;
    if (std::filesystem::is_directory(hdri_dir, error)) {
        for (const auto& entry : std::filesystem::directory_iterator(hdri_dir, error)) {
            if (!entry.is_regular_file() || !is_hdri_file(entry.path())) {
                continue;
            }
            const auto file_name = entry.path().filename().string();
            environments.push_back({.id = hdri_id_for(file_name), .name = entry.path().stem().string(), .bundled = true});
        }
    }
    std::ranges::sort(environments, [](const auto& left, const auto& right) { return left.name < right.name; });
    return environments;
}

[[nodiscard]] std::string default_hdri_environment_id(const std::vector<HdriEnvironment>& environments)
{
    const auto preferred = hdri_id_for(std::string(kDefaultHdriFile));
    if (std::ranges::any_of(environments, [&](const auto& environment) { return environment.id == preferred; })) {
        return preferred;
    }
    return environments.empty() ? std::string{} : environments.front().id;
}

// The stage references HDRI files relative to the asset search path.
[[nodiscard]] std::filesystem::path hdri_relative_path(std::string_view environment_id)
{
    return std::filesystem::path("HDRI") / environment_id.substr(kHdriIdPrefix.size());
}

[[nodiscard]] const project::ViewportState& active_viewport(const project::Project& project)
{
    switch (project.active_workspace) {
    case project::Workspace::Module:
        return project.module_workspace.viewport;
    case project::Workspace::Plant:
        return project.plant_workspace.viewport;
    case project::Workspace::Ecosystem:
        return project.ecosystem_workspace.viewport;
    }
    std::unreachable();
}

[[nodiscard]] project::ViewportState& active_viewport(project::Project& project)
{
    switch (project.active_workspace) {
    case project::Workspace::Module:
        return project.module_workspace.viewport;
    case project::Workspace::Plant:
        return project.plant_workspace.viewport;
    case project::Workspace::Ecosystem:
        return project.ecosystem_workspace.viewport;
    }
    std::unreachable();
}

[[nodiscard]] ApplicationError make_error(ApplicationError::Code code, std::string message)
{
    return {.code = code, .message = std::move(message)};
}

[[nodiscard]] ApplicationError from_import_error(const import::ImportError& error)
{
    return make_error(ApplicationError::Code::Import, error.message);
}

[[nodiscard]] ApplicationError from_project_error(const project::ProjectError& error)
{
    return make_error(ApplicationError::Code::Project, error.message);
}

[[nodiscard]] ApplicationError from_growth_error(const growth::GrowthError& error)
{
    return make_error(ApplicationError::Code::Growth, error.message);
}

[[nodiscard]] const growth::BranchModulePrototype* prototype_by_id(const import::BranchModulePrototypeLibrary& library,
                                                                   std::size_t prototype_id)
{
    const auto found = std::ranges::find_if(
        library.prototypes, [prototype_id](const auto& prototype) { return prototype.id == prototype_id; });
    return found == library.prototypes.end() ? nullptr : &*found;
}

[[nodiscard]] Result<std::size_t> default_prototype_id(const import::BranchModulePrototypeLibrary& library)
{
    if (library.prototypes.empty()) {
        return std::unexpected(make_error(ApplicationError::Code::Import, "branch module prototype library is empty"));
    }
    if (const auto cube = import::prototype_id_by_name(library, kDefaultPrototypeName)) {
        return *cube;
    }
    return library.prototypes.front().id;
}

[[nodiscard]] PrototypeTreeItem build_node_tree(const growth::BranchModulePrototype& prototype, std::size_t node_id)
{
    PrototypeTreeItem item{
        .kind = PrototypeTreeItem::Kind::Node,
        .id = node_id,
        .label =
            "Node " + std::to_string(node_id) + " age " + std::to_string(prototype.nodes[node_id].physiological_age),
        .children = {},
    };

    for (const std::size_t segment_id : prototype.child_segments_by_node[node_id]) {
        const auto& segment = prototype.segments[segment_id];
        PrototypeTreeItem segment_item{
            .kind = PrototypeTreeItem::Kind::Segment,
            .id = segment_id,
            .label = "Segment " + std::to_string(segment_id) + " length " + std::to_string(segment.max_length),
            .children = {},
        };
        segment_item.children.push_back(build_node_tree(prototype, segment.child_node));
        item.children.push_back(std::move(segment_item));
    }

    return item;
}

struct WorkspaceFacts {
    const project::PlantType& plant_type;
    growth::BranchModulePrototype prepared_prototype;
    float fully_grown_age = 0.0F;
};

[[nodiscard]] Result<WorkspaceFacts> workspace_facts(const import::BranchModulePrototypeLibrary& prototype_library,
                                                     const project::Project& project, project::Workspace workspace)
{
    std::size_t prototype_id = 0;
    std::string_view plant_type_id;
    std::string_view missing_prototype;
    std::string_view missing_plant_type;
    switch (workspace) {
    case project::Workspace::Module:
        prototype_id = project.module_workspace.prototype_id;
        plant_type_id = project.module_workspace.plant_type_id;
        missing_prototype = "active branch module prototype does not exist";
        missing_plant_type = "active plant type does not exist";
        break;
    case project::Workspace::Plant:
        prototype_id = project.plant_workspace.root_prototype_id;
        plant_type_id = project.plant_workspace.plant_type_id;
        missing_prototype = "Plant root prototype does not exist";
        missing_plant_type = "Plant plant type does not exist";
        break;
    case project::Workspace::Ecosystem:
        return std::unexpected(
            make_error(ApplicationError::Code::InvalidCommand, "Ecosystem workspace has no growth preview"));
    }

    const auto* prototype = prototype_by_id(prototype_library, prototype_id);
    if (prototype == nullptr) {
        return std::unexpected(make_error(ApplicationError::Code::NotFound, std::string(missing_prototype)));
    }
    const auto* plant_type = project::plant_type_by_id(project, plant_type_id);
    if (plant_type == nullptr) {
        return std::unexpected(make_error(ApplicationError::Code::NotFound, std::string(missing_plant_type)));
    }
    auto prepared = growth::prepare_branch_module_prototype(*prototype, plant_type->parameters);
    if (!prepared) {
        return std::unexpected(from_growth_error(prepared.error()));
    }
    auto fully_grown = growth::fully_grown_age(*prepared, plant_type->parameters);
    if (!fully_grown) {
        return std::unexpected(from_growth_error(fully_grown.error()));
    }
    return WorkspaceFacts{
        .plant_type = *plant_type,
        .prepared_prototype = std::move(*prepared),
        .fully_grown_age = *fully_grown,
    };
}

[[nodiscard]] Result<growth::PlantSimulation>
make_plant_simulation(const import::BranchModulePrototypeLibrary& prototype_library, const project::Project& project)
{
    const auto* plant_type = project::plant_type_by_id(project, project.plant_workspace.plant_type_id);
    if (plant_type == nullptr) {
        return std::unexpected(make_error(ApplicationError::Code::NotFound, "Plant plant type does not exist"));
    }
    auto simulation = growth::PlantSimulation::create(
        prototype_library, plant_type->parameters, project.plant_workspace.root_prototype_id);
    if (!simulation) {
        return std::unexpected(from_growth_error(simulation.error()));
    }
    const auto& diagnostics = project.plant_workspace.diagnostics;
    auto configured = simulation->set_flow_diagnostics({
        .accumulated_light = diagnostics.accumulated_light_flow_visible,
        .vigor = diagnostics.vigor_flow_visible,
    });
    if (!configured) {
        return std::unexpected(from_growth_error(configured.error()));
    }
    return std::move(*simulation);
}

} // namespace

DesktopSession::DesktopSession(DesktopSessionOptions options,
                               import::BranchModulePrototypeLibrary prototype_library, project::Project project,
                               growth::PlantSimulation plant_simulation)
    : options_(std::move(options))
    , prototype_library_(std::move(prototype_library))
    , project_(std::move(project))
    , plant_simulation_(std::move(plant_simulation))
{
    module_camera_needs_frame_ = !project_.module_workspace.viewport.orbit_initialized;
    plant_camera_needs_frame_ = !project_.plant_workspace.viewport.orbit_initialized;
}

Result<DesktopSession> DesktopSession::create(DesktopSessionOptions options)
{
    auto library = import::load_branch_module_prototype_library_from_obj(options.prototype_asset_path,
                                                                         kPrototypeLibraryGeometryScale);
    if (!library) {
        return std::unexpected(from_import_error(library.error()));
    }

    auto default_branch_module_prototype_id = default_prototype_id(*library);
    if (!default_branch_module_prototype_id) {
        return std::unexpected(default_branch_module_prototype_id.error());
    }

    const auto environments = enumerate_hdri_environments(options.asset_root_path);
    const auto default_hdri = default_hdri_environment_id(environments);
    if (default_hdri.empty()) {
        return std::unexpected(make_error(ApplicationError::Code::Project, "HDRI environment library is empty"));
    }

    const bool creating_project = !std::filesystem::exists(options.project_path);
    project::Result<project::Project> loaded_project =
        creating_project
            ? project::Result<project::Project>{
                  project::make_default_project(*default_branch_module_prototype_id, default_hdri)}
            : project::load_project(options.project_path);
    if (!loaded_project) {
        return std::unexpected(from_project_error(loaded_project.error()));
    }

    if (prototype_by_id(*library, loaded_project->module_workspace.prototype_id) == nullptr) {
        return std::unexpected(make_error(ApplicationError::Code::Project,
                                          "Module workspace prototype does not exist"));
    }
    if (prototype_by_id(*library, loaded_project->plant_workspace.root_prototype_id) == nullptr) {
        return std::unexpected(
            make_error(ApplicationError::Code::Project, "Plant workspace root prototype does not exist"));
    }
    const auto environment_exists = [&](const project::ViewportState& viewport) {
        return std::ranges::any_of(environments, [&](const HdriEnvironment& environment) {
            return environment.id == viewport.active_hdri_environment_id;
        });
    };
    if (!environment_exists(loaded_project->module_workspace.viewport) ||
        !environment_exists(loaded_project->plant_workspace.viewport) ||
        !environment_exists(loaded_project->ecosystem_workspace.viewport)) {
        return std::unexpected(
            make_error(ApplicationError::Code::Project, "workspace references an unknown HDRI environment"));
    }

    auto facts = workspace_facts(*library, *loaded_project, project::Workspace::Module);
    if (!facts) {
        return std::unexpected(facts.error());
    }
    if (creating_project) {
        loaded_project->module_workspace.physiological_age = facts->fully_grown_age;
        auto saved = project::save_project(options.project_path, *loaded_project);
        if (!saved) {
            return std::unexpected(from_project_error(saved.error()));
        }
    }

    auto plant_simulation = make_plant_simulation(*library, *loaded_project);
    if (!plant_simulation) {
        return std::unexpected(plant_simulation.error());
    }

    return DesktopSession(std::move(options), std::move(*library), std::move(*loaded_project),
                          std::move(*plant_simulation));
}

Result<AppStateView> DesktopSession::state() const
{
    auto facts = workspace_facts(prototype_library_, project_, project::Workspace::Module);
    if (!facts) {
        return std::unexpected(facts.error());
    }

    AppStateView view;
    view.active_workspace = project::to_string(project_.active_workspace);
    view.workspace_previews = {
        {.workspace = "plant", .implemented = true},
        {.workspace = "ecosystem", .implemented = false},
    };
    view.active_prototype_id = project_.module_workspace.prototype_id;
    view.active_plant_type_id = project_.module_workspace.plant_type_id;
    view.module_physiological_age = project_.module_workspace.physiological_age;
    view.fully_grown_age = facts->fully_grown_age;

    view.prototypes.reserve(prototype_library_.prototypes.size());
    for (const auto& prototype : prototype_library_.prototypes) {
        view.prototypes.push_back({
            .id = prototype.id,
            .name = prototype.name,
            .node_count = prototype.nodes.size(),
            .segment_count = prototype.segments.size(),
        });
    }

    view.plant_types.reserve(project_.plant_type_library.plant_types.size());
    for (const auto& plant_type : project_.plant_type_library.plant_types) {
        view.plant_types.push_back({
            .id = plant_type.id,
            .name = plant_type.name,
        });
    }
    return view;
}

Result<PrototypeTreeView> DesktopSession::prototype_tree() const
{
    auto facts = workspace_facts(prototype_library_, project_, project::Workspace::Module);
    if (!facts) {
        return std::unexpected(facts.error());
    }
    return PrototypeTreeView{.root = build_node_tree(facts->prepared_prototype, facts->prepared_prototype.root_node)};
}

Result<growth::GrowthSnapshot> DesktopSession::growth_snapshot() const
{
    auto facts = workspace_facts(prototype_library_, project_, project::Workspace::Module);
    if (!facts) {
        return std::unexpected(facts.error());
    }
    auto snapshot = growth::make_growth_snapshot(facts->prepared_prototype, facts->plant_type.parameters,
                                                 project_.module_workspace.physiological_age);
    if (!snapshot) {
        return std::unexpected(from_growth_error(snapshot.error()));
    }
    return *snapshot;
}

Result<GrowthSnapshotSummary> DesktopSession::growth_snapshot_summary() const
{
    auto snapshot = growth_snapshot();
    if (!snapshot) {
        return std::unexpected(snapshot.error());
    }

    GrowthSnapshotSummary summary;
    summary.module_physiological_age = snapshot->module_physiological_age;
    summary.growth_rate = snapshot->growth_rate;
    summary.visible_segment_count = snapshot->segments.size();
    for (const auto& segment : snapshot->segments) {
        if (segment.state == growth::SegmentState::Growing) {
            ++summary.growing_segment_count;
        } else {
            ++summary.mature_segment_count;
        }
        summary.max_diameter = std::max(summary.max_diameter, segment.diameter);
    }
    return summary;
}

Result<ModulePreviewSnapshot> DesktopSession::module_preview_snapshot() const
{
    auto facts = workspace_facts(prototype_library_, project_, project::Workspace::Module);
    if (!facts) {
        return std::unexpected(facts.error());
    }
    auto snapshot = growth::make_growth_snapshot(facts->prepared_prototype, facts->plant_type.parameters,
                                                 project_.module_workspace.physiological_age);
    if (!snapshot) {
        return std::unexpected(from_growth_error(snapshot.error()));
    }
    auto camera_snapshot =
        growth::make_growth_snapshot(facts->prepared_prototype, facts->plant_type.parameters, facts->fully_grown_age);
    if (!camera_snapshot) {
        return std::unexpected(from_growth_error(camera_snapshot.error()));
    }
    return ModulePreviewSnapshot{
        .snapshot = std::move(*snapshot),
        .camera_snapshot = std::move(*camera_snapshot),
        .prepared_prototype = std::move(facts->prepared_prototype),
    };
}

Result<PlantStateView> DesktopSession::plant_state() const
{
    const auto snapshot = plant_simulation_.snapshot();
    const auto& root = snapshot.modules.front();
    const auto& diagnostics = project_.plant_workspace.diagnostics;
    return PlantStateView{
        .plant_age = snapshot.plant_age,
        .root_physiological_age = root.physiological_age,
        .root_fully_grown_age = root.fully_grown_age,
        .timestep = project_.plant_workspace.simulation_timestep,
        .paused = true,
        .root_prototype_id = project_.plant_workspace.root_prototype_id,
        .plant_type_id = project_.plant_workspace.plant_type_id,
        .module_diagnostic_labels_visible = diagnostics.module_diagnostic_labels_visible,
        .direct_light_bounding_spheres_visible = diagnostics.direct_light_bounding_spheres_visible,
        .accumulated_light_flow_visible = diagnostics.accumulated_light_flow_visible,
        .vigor_flow_visible = diagnostics.vigor_flow_visible,
        .mature_terminal_markers_visible = diagnostics.mature_terminal_markers_visible,
        .direct_light_exposure = root.direct_light_exposure,
        .accumulated_light = root.accumulated_light,
        .vigor = root.vigor,
        .growth_rate = root.growth_rate,
    };
}

Result<PlantPreviewSnapshot> DesktopSession::plant_preview_snapshot() const
{
    auto facts = workspace_facts(prototype_library_, project_, project::Workspace::Plant);
    if (!facts) {
        return std::unexpected(facts.error());
    }
    auto mature_root =
        growth::make_growth_snapshot(facts->prepared_prototype, facts->plant_type.parameters, facts->fully_grown_age);
    if (!mature_root) {
        return std::unexpected(from_growth_error(mature_root.error()));
    }
    const auto root_offset =
        growth::scale(facts->prepared_prototype.nodes[facts->prepared_prototype.root_node].position, -1.0F);
    for (auto& segment : mature_root->segments) {
        segment.parent_position = growth::add(segment.parent_position, root_offset);
        segment.child_position = growth::add(segment.child_position, root_offset);
    }
    return PlantPreviewSnapshot{
        .snapshot = plant_simulation_.snapshot(),
        .mature_root_snapshot = std::move(*mature_root),
        .prepared_root = std::move(facts->prepared_prototype),
    };
}

PreviewEnvironment DesktopSession::preview_environment() const
{
    const auto& viewport = active_viewport(project_);
    return {
        .asset_search_path = options_.asset_root_path,
        .hdri_texture_path = hdri_relative_path(viewport.active_hdri_environment_id),
        .hdri_visible = viewport.hdri_backdrop_visible,
        .guides_visible = viewport.guides_visible,
        .world_origin_axes_visible = viewport.world_origin_axes_visible,
    };
}

Result<void> DesktopSession::set_active_workspace(std::string_view workspace)
{
    project::Workspace active_workspace;
    if (workspace == "module") {
        active_workspace = project::Workspace::Module;
    } else if (workspace == "plant") {
        active_workspace = project::Workspace::Plant;
    } else if (workspace == "ecosystem") {
        return std::unexpected(make_error(ApplicationError::Code::InvalidCommand,
                                          "workspace is not implemented: " + std::string(workspace)));
    } else {
        return std::unexpected(
            make_error(ApplicationError::Code::InvalidCommand, "unknown workspace " + std::string(workspace)));
    }

    auto updated_project = project_;
    updated_project.active_workspace = active_workspace;
    return commit_project(std::move(updated_project));
}

Result<void> DesktopSession::plant_step()
{
    auto stepped = plant_simulation_.step(project_.plant_workspace.simulation_timestep);
    if (!stepped) {
        return std::unexpected(from_growth_error(stepped.error()));
    }
    return {};
}

Result<void> DesktopSession::plant_reset()
{
    auto simulation = make_plant_simulation(prototype_library_, project_);
    if (!simulation) {
        return std::unexpected(simulation.error());
    }
    plant_simulation_ = std::move(*simulation);
    plant_camera_needs_frame_ = true;
    return {};
}

Result<void> DesktopSession::set_plant_timestep(float timestep)
{
    if (!std::isfinite(timestep) || timestep <= 0.0F) {
        return std::unexpected(
            make_error(ApplicationError::Code::InvalidCommand, "Plant timestep must be finite and positive"));
    }
    auto updated_project = project_;
    updated_project.plant_workspace.simulation_timestep = timestep;
    return commit_project(std::move(updated_project));
}

Result<void> DesktopSession::update_plant_diagnostics(PlantDiagnosticsUpdate diagnostics)
{
    auto updated_project = project_;
    updated_project.plant_workspace.diagnostics.module_diagnostic_labels_visible =
        diagnostics.module_diagnostic_labels_visible;
    updated_project.plant_workspace.diagnostics.direct_light_bounding_spheres_visible =
        diagnostics.direct_light_bounding_spheres_visible;
    updated_project.plant_workspace.diagnostics.accumulated_light_flow_visible =
        diagnostics.accumulated_light_flow_visible;
    updated_project.plant_workspace.diagnostics.vigor_flow_visible = diagnostics.vigor_flow_visible;
    updated_project.plant_workspace.diagnostics.mature_terminal_markers_visible =
        diagnostics.mature_terminal_markers_visible;

    auto updated_simulation = plant_simulation_;
    auto configured = updated_simulation.set_flow_diagnostics({
        .accumulated_light = diagnostics.accumulated_light_flow_visible,
        .vigor = diagnostics.vigor_flow_visible,
    });
    if (!configured) {
        return std::unexpected(from_growth_error(configured.error()));
    }
    auto committed = commit_project(std::move(updated_project));
    if (!committed) {
        return committed;
    }
    plant_simulation_ = std::move(updated_simulation);
    return {};
}

Result<project::PlantType> DesktopSession::plant_type(std::string_view plant_type_id) const
{
    const auto* found = project::plant_type_by_id(project_, plant_type_id);
    if (found == nullptr) {
        return std::unexpected(
            make_error(ApplicationError::Code::NotFound, "unknown plant type id " + std::string(plant_type_id)));
    }
    return *found;
}

Result<void> DesktopSession::set_active_prototype(std::size_t prototype_id)
{
    if (prototype_by_id(prototype_library_, prototype_id) == nullptr) {
        return std::unexpected(make_error(ApplicationError::Code::NotFound,
                                          "unknown branch module prototype id " + std::to_string(prototype_id)));
    }
    auto updated_project = project_;
    updated_project.module_workspace.prototype_id = prototype_id;
    auto clamped = clamp_module_age_to_active_range(updated_project);
    if (!clamped) {
        return std::unexpected(clamped.error());
    }
    return commit_project(std::move(updated_project));
}

Result<void> DesktopSession::set_active_plant_type(std::string_view plant_type_id)
{
    if (project::plant_type_by_id(project_, plant_type_id) == nullptr) {
        return std::unexpected(
            make_error(ApplicationError::Code::NotFound, "unknown plant type id " + std::string(plant_type_id)));
    }
    auto updated_project = project_;
    updated_project.module_workspace.plant_type_id = std::string(plant_type_id);
    auto clamped = clamp_module_age_to_active_range(updated_project);
    if (!clamped) {
        return std::unexpected(clamped.error());
    }
    return commit_project(std::move(updated_project));
}

Result<void> DesktopSession::set_module_physiological_age(float module_physiological_age)
{
    if (!std::isfinite(module_physiological_age) || module_physiological_age < 0.0F) {
        return std::unexpected(make_error(ApplicationError::Code::InvalidCommand,
                                          "module physiological age must be finite and non-negative"));
    }
    auto updated_project = project_;
    updated_project.module_workspace.physiological_age = module_physiological_age;
    auto clamped = clamp_module_age_to_active_range(updated_project);
    if (!clamped) {
        return std::unexpected(clamped.error());
    }
    return commit_project(std::move(updated_project));
}

Result<project::PlantType> DesktopSession::create_plant_type(std::string name, char preset_key)
{
    auto updated_project = project_;
    const std::string id = project::next_plant_type_id(updated_project.plant_type_library);
    auto plant_type = project::create_plant_type_from_preset(id, std::move(name), preset_key);
    if (!plant_type) {
        return std::unexpected(from_project_error(plant_type.error()));
    }
    updated_project.plant_type_library.plant_types.push_back(*plant_type);
    auto committed = commit_project(std::move(updated_project));
    if (!committed) {
        return std::unexpected(committed.error());
    }
    return *plant_type;
}

Result<void> DesktopSession::delete_plant_type(std::string_view plant_type_id)
{
    auto updated_project = project_;
    const bool module_selected = updated_project.module_workspace.plant_type_id == plant_type_id;
    const bool plant_selected = updated_project.plant_workspace.plant_type_id == plant_type_id;
    auto deleted = project::delete_plant_type(updated_project, plant_type_id);
    if (!deleted) {
        return std::unexpected(from_project_error(deleted.error()));
    }
    return commit_plant_type_change(std::move(updated_project), module_selected, plant_selected);
}

Result<void> DesktopSession::update_plant_type(project::PlantType plant_type)
{
    if (!growth::plant_type_parameters_are_valid(plant_type.parameters)) {
        return std::unexpected(make_error(ApplicationError::Code::InvalidCommand, "plant type parameters are invalid"));
    }
    auto updated_project = project_;
    auto* existing = project::plant_type_by_id(updated_project, plant_type.id);
    if (existing == nullptr) {
        return std::unexpected(make_error(ApplicationError::Code::NotFound, "unknown plant type id " + plant_type.id));
    }
    const bool module_selected = updated_project.module_workspace.plant_type_id == plant_type.id;
    const bool plant_selected = updated_project.plant_workspace.plant_type_id == plant_type.id;
    *existing = std::move(plant_type);
    return commit_plant_type_change(std::move(updated_project), module_selected, plant_selected);
}

Result<void> DesktopSession::commit_plant_type_change(project::Project updated_project, bool module_selected,
                                                      bool plant_selected)
{
    if (module_selected) {
        auto clamped = clamp_module_age_to_active_range(updated_project);
        if (!clamped) {
            return std::unexpected(clamped.error());
        }
    }

    std::optional<growth::PlantSimulation> updated_simulation;
    if (plant_selected) {
        auto simulation = make_plant_simulation(prototype_library_, updated_project);
        if (!simulation) {
            return std::unexpected(simulation.error());
        }
        updated_simulation.emplace(std::move(*simulation));
    }
    auto committed = commit_project(std::move(updated_project));
    if (!committed) {
        return committed;
    }
    if (updated_simulation) {
        plant_simulation_ = std::move(*updated_simulation);
        plant_camera_needs_frame_ = true;
    }
    return {};
}

ViewportAppearance DesktopSession::viewport_preferences() const
{
    const auto& viewport = active_viewport(project_);
    return {
        .guides_visible = viewport.guides_visible,
        .world_origin_axes_visible = viewport.world_origin_axes_visible,
        .hdri_backdrop_visible = viewport.hdri_backdrop_visible,
        .active_hdri_environment_id = viewport.active_hdri_environment_id,
    };
}

project::OrbitState DesktopSession::active_orbit() const
{
    return active_viewport(project_).orbit;
}

bool DesktopSession::active_camera_needs_frame() const
{
    switch (project_.active_workspace) {
    case project::Workspace::Module:
        return module_camera_needs_frame_;
    case project::Workspace::Plant:
        return plant_camera_needs_frame_;
    case project::Workspace::Ecosystem:
        return false;
    }
    std::unreachable();
}

std::vector<HdriEnvironment> DesktopSession::hdri_environments() const
{
    return enumerate_hdri_environments(options_.asset_root_path);
}

Result<void> DesktopSession::update_viewport_preferences(ViewportAppearance appearance)
{
    const auto environments = hdri_environments();
    if (!std::ranges::any_of(environments, [&](const auto& environment) {
            return environment.id == appearance.active_hdri_environment_id;
        })) {
        return std::unexpected(make_error(ApplicationError::Code::NotFound, "unknown HDRI environment"));
    }
    auto updated_project = project_;
    auto& viewport = active_viewport(updated_project);
    viewport.guides_visible = appearance.guides_visible;
    viewport.world_origin_axes_visible = appearance.world_origin_axes_visible;
    viewport.hdri_backdrop_visible = appearance.hdri_backdrop_visible;
    viewport.active_hdri_environment_id = std::move(appearance.active_hdri_environment_id);
    return commit_project(std::move(updated_project));
}

Result<void> DesktopSession::update_active_orbit(project::OrbitState orbit)
{
    auto updated_project = project_;
    active_viewport(updated_project).orbit = orbit;
    active_viewport(updated_project).orbit_initialized = true;
    auto committed = commit_project(std::move(updated_project));
    if (!committed) {
        return committed;
    }
    if (project_.active_workspace == project::Workspace::Module) {
        module_camera_needs_frame_ = false;
    } else if (project_.active_workspace == project::Workspace::Plant) {
        plant_camera_needs_frame_ = false;
    }
    return {};
}

Result<void> DesktopSession::commit_project(project::Project project)
{
    auto saved = project::save_project(options_.project_path, project);
    if (!saved) {
        return std::unexpected(from_project_error(saved.error()));
    }
    project_ = std::move(project);
    return {};
}

Result<void> DesktopSession::clamp_module_age_to_active_range(project::Project& project) const
{
    auto facts = workspace_facts(prototype_library_, project, project::Workspace::Module);
    if (!facts) {
        return std::unexpected(facts.error());
    }
    project.module_workspace.physiological_age =
        std::min(project.module_workspace.physiological_age, facts->fully_grown_age);
    return {};
}

std::string to_string(PrototypeTreeItem::Kind kind)
{
    switch (kind) {
    case PrototypeTreeItem::Kind::Node:
        return "node";
    case PrototypeTreeItem::Kind::Segment:
        return "segment";
    }
    std::unreachable();
}

} // namespace toi::model
