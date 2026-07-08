#include "toi/app/application_controller.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace toi::app {
namespace {

constexpr std::string_view kDefaultPrototypeName = "Cube.008";
constexpr std::string_view kHdriIdPrefix = "hdri:";
constexpr std::string_view kDefaultHdriFile = "meadow_2_4k.exr";

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
    if (!environment_id.starts_with(kHdriIdPrefix)) {
        return std::filesystem::path("HDRI") / kDefaultHdriFile;
    }
    return std::filesystem::path("HDRI") / environment_id.substr(kHdriIdPrefix.size());
}

[[nodiscard]] std::filesystem::path viewport_preferences_path(const std::filesystem::path& project_path)
{
    auto parent = project_path.parent_path();
    return (parent.empty() ? std::filesystem::path(".") : parent) / "viewport-preferences.json";
}

[[nodiscard]] ViewportPreferences load_viewport_preferences(const std::filesystem::path& project_path,
                                                            const std::vector<HdriEnvironment>& environments)
{
    ViewportPreferences preferences;
    preferences.active_hdri_environment_id = default_hdri_environment_id(environments);

    const auto path = viewport_preferences_path(project_path);
    std::ifstream input(path);
    if (input) {
        try {
            const auto document = nlohmann::json::parse(input);
            preferences.guides_visible = document.value("guides_visible", preferences.guides_visible);
            preferences.world_origin_axes_visible =
                document.value("world_origin_axes_visible", preferences.world_origin_axes_visible);
            preferences.hdri_backdrop_visible =
                document.value("hdri_backdrop_visible", preferences.hdri_backdrop_visible);
            const auto active = document.value("active_hdri_environment_id", preferences.active_hdri_environment_id);
            if (std::ranges::any_of(environments, [&](const auto& env) { return env.id == active; })) {
                preferences.active_hdri_environment_id = active;
            }
        } catch (const std::exception&) {
            // Ignore malformed preferences and keep defaults.
        }
    }
    return preferences;
}

void save_viewport_preferences(const std::filesystem::path& project_path, const ViewportPreferences& preferences)
{
    const nlohmann::json document{
        {"guides_visible", preferences.guides_visible},
        {"world_origin_axes_visible", preferences.world_origin_axes_visible},
        {"hdri_backdrop_visible", preferences.hdri_backdrop_visible},
        {"active_hdri_environment_id", preferences.active_hdri_environment_id},
    };
    std::ofstream output(viewport_preferences_path(project_path));
    output << document.dump(2) << '\n';
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

[[nodiscard]] ApplicationError from_plant_error(const plant::PlantError& error)
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

struct ModuleWorkspaceFacts {
    const project::PlantType& plant_type;
    growth::BranchModulePrototype prepared_prototype;
    float fully_grown_age = 0.0F;
};

[[nodiscard]] Result<ModuleWorkspaceFacts>
module_workspace_facts(const import::BranchModulePrototypeLibrary& prototype_library, const project::Project& project)
{
    const auto* prototype = prototype_by_id(prototype_library, project.active_branch_module_prototype_id);
    if (prototype == nullptr) {
        return std::unexpected(
            make_error(ApplicationError::Code::NotFound, "active branch module prototype does not exist"));
    }

    const auto* plant_type = project::active_plant_type(project);
    if (plant_type == nullptr) {
        return std::unexpected(make_error(ApplicationError::Code::NotFound, "active plant type does not exist"));
    }

    auto prepared = growth::prepare_branch_module_prototype(*prototype, plant_type->parameters);
    if (!prepared) {
        return std::unexpected(from_growth_error(prepared.error()));
    }

    auto fully_grown = growth::fully_grown_age(*prepared, plant_type->parameters);
    if (!fully_grown) {
        return std::unexpected(from_growth_error(fully_grown.error()));
    }

    return ModuleWorkspaceFacts{
        .plant_type = *plant_type,
        .prepared_prototype = std::move(*prepared),
        .fully_grown_age = *fully_grown,
    };
}

} // namespace

ApplicationController::ApplicationController(ApplicationControllerOptions options,
                                             import::BranchModulePrototypeLibrary prototype_library,
                                             project::Project project, float module_physiological_age)
    : options_(std::move(options))
    , prototype_library_(std::move(prototype_library))
    , project_(std::move(project))
    , module_physiological_age_(module_physiological_age)
{
}

Result<ApplicationController> ApplicationController::create(ApplicationControllerOptions options)
{
    auto library = import::load_branch_module_prototype_library_from_obj(options.prototype_asset_path);
    if (!library) {
        return std::unexpected(from_import_error(library.error()));
    }

    auto default_branch_module_prototype_id = default_prototype_id(*library);
    if (!default_branch_module_prototype_id) {
        return std::unexpected(default_branch_module_prototype_id.error());
    }

    auto loaded_project = project::load_or_create_project(options.project_path, *default_branch_module_prototype_id);
    if (!loaded_project) {
        return std::unexpected(from_project_error(loaded_project.error()));
    }

    if (prototype_by_id(*library, loaded_project->active_branch_module_prototype_id) == nullptr) {
        loaded_project->active_branch_module_prototype_id = *default_branch_module_prototype_id;
        auto saved = project::save_project(options.project_path, *loaded_project);
        if (!saved) {
            return std::unexpected(from_project_error(saved.error()));
        }
    }

    ApplicationController controller(std::move(options), std::move(*library), std::move(*loaded_project), 0.0F);
    auto facts = module_workspace_facts(controller.prototype_library_, controller.project_);
    if (!facts) {
        return std::unexpected(facts.error());
    }
    controller.module_physiological_age_ = facts->fully_grown_age;
    controller.viewport_preferences_ =
        load_viewport_preferences(controller.options_.project_path, controller.hdri_environments());
    return controller;
}

Result<void> ApplicationController::save_project() const
{
    auto saved = project::save_project(options_.project_path, project_);
    if (!saved) {
        return std::unexpected(from_project_error(saved.error()));
    }
    return {};
}

Result<AppStateView> ApplicationController::state() const
{
    auto facts = module_workspace_facts(prototype_library_, project_);
    if (!facts) {
        return std::unexpected(facts.error());
    }

    AppStateView view;
    view.active_workspace = active_workspace_;
    view.workspace_previews = {
        {.workspace = "plant", .implemented = true},
        {.workspace = "ecosystem", .implemented = false},
    };
    view.active_prototype_id = project_.active_branch_module_prototype_id;
    view.active_plant_type_id = project_.plant_type_library.active_plant_type_id;
    view.module_physiological_age = module_physiological_age_;
    view.fully_grown_age = facts->fully_grown_age;
    view.plant_physiological_age = plant_physiological_age_;
    const auto* active_plant = project::active_plant_type(project_);
    view.plant_fully_grown_age = active_plant != nullptr ? active_plant->parameters.plant_max_age : 0.0F;

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

Result<PrototypeTreeView> ApplicationController::prototype_tree() const
{
    auto facts = module_workspace_facts(prototype_library_, project_);
    if (!facts) {
        return std::unexpected(facts.error());
    }
    return PrototypeTreeView{.root = build_node_tree(facts->prepared_prototype, facts->prepared_prototype.root_node)};
}

Result<growth::GrowthSnapshot> ApplicationController::growth_snapshot() const
{
    auto facts = module_workspace_facts(prototype_library_, project_);
    if (!facts) {
        return std::unexpected(facts.error());
    }
    auto snapshot = growth::make_growth_snapshot(facts->prepared_prototype, facts->plant_type.parameters,
                                                 module_physiological_age_);
    if (!snapshot) {
        return std::unexpected(from_growth_error(snapshot.error()));
    }
    return *snapshot;
}

Result<GrowthSnapshotSummary> ApplicationController::growth_snapshot_summary() const
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

Result<render::GrowthPreviewStageProjection>
ApplicationController::growth_preview_stage_projection(render::GrowthPreviewStageOptions options) const
{
    auto facts = module_workspace_facts(prototype_library_, project_);
    if (!facts) {
        return std::unexpected(facts.error());
    }
    auto snapshot = growth::make_growth_snapshot(facts->prepared_prototype, facts->plant_type.parameters,
                                                 module_physiological_age_);
    if (!snapshot) {
        return std::unexpected(from_growth_error(snapshot.error()));
    }
    auto camera_snapshot =
        growth::make_growth_snapshot(facts->prepared_prototype, facts->plant_type.parameters, facts->fully_grown_age);
    if (!camera_snapshot) {
        return std::unexpected(from_growth_error(camera_snapshot.error()));
    }
    options.asset_search_path = options_.asset_root_path;
    options.hdri_visible = viewport_preferences_.hdri_backdrop_visible;
    options.hdri_texture_path = hdri_relative_path(viewport_preferences_.active_hdri_environment_id);
    return render::make_growth_preview_stage_projection(*snapshot, *camera_snapshot, facts->prepared_prototype,
                                                        options);
}

Result<plant::PlantArchitecture> ApplicationController::plant_architecture() const
{
    const auto* active_plant = project::active_plant_type(project_);
    if (active_plant == nullptr) {
        return std::unexpected(make_error(ApplicationError::Code::NotFound, "active plant type does not exist"));
    }
    auto architecture = plant::develop_plant(active_plant->parameters, prototype_library_, plant_physiological_age_);
    if (!architecture) {
        return std::unexpected(from_plant_error(architecture.error()));
    }
    return std::move(*architecture);
}

Result<PlantGrowthSummary> ApplicationController::plant_growth_summary() const
{
    const auto* active_plant = project::active_plant_type(project_);
    if (active_plant == nullptr) {
        return std::unexpected(make_error(ApplicationError::Code::NotFound, "active plant type does not exist"));
    }
    auto architecture = plant::develop_plant(active_plant->parameters, prototype_library_, plant_physiological_age_);
    if (!architecture) {
        return std::unexpected(from_plant_error(architecture.error()));
    }
    const auto summary = plant::summarize(*architecture);
    return PlantGrowthSummary{
        .plant_physiological_age = architecture->plant_age,
        .plant_fully_grown_age = active_plant->parameters.plant_max_age,
        .module_count = summary.module_count,
        .visible_segment_count = summary.visible_segment_count,
        .max_diameter = summary.max_diameter,
        .senescent = summary.senescent,
    };
}

Result<render::GrowthPreviewStageProjection>
ApplicationController::plant_preview_stage_projection(render::GrowthPreviewStageOptions options) const
{
    auto architecture = plant_architecture();
    if (!architecture) {
        return std::unexpected(architecture.error());
    }
    options.asset_search_path = options_.asset_root_path;
    options.hdri_visible = viewport_preferences_.hdri_backdrop_visible;
    options.hdri_texture_path = hdri_relative_path(viewport_preferences_.active_hdri_environment_id);
    return render::make_plant_preview_stage_projection(*architecture, options);
}

Result<render::GrowthPreviewStageProjection>
ApplicationController::plant_preset_preview_stage_projection(char preset_key, float plant_age,
                                                            render::GrowthPreviewStageOptions options) const
{
    const auto preset = growth::plant_type_preset_by_key(preset_key);
    if (!preset) {
        return std::unexpected(make_error(ApplicationError::Code::NotFound,
                                          std::string("unknown plant type preset '") + preset_key + "'"));
    }
    const float requested = plant_age <= 0.0F ? preset->plant_max_age : plant_age;
    const float clamped = std::clamp(requested, 0.0F, preset->plant_max_age);
    auto architecture = plant::develop_plant(*preset, prototype_library_, clamped);
    if (!architecture) {
        return std::unexpected(from_plant_error(architecture.error()));
    }
    options.asset_search_path = options_.asset_root_path;
    options.hdri_visible = viewport_preferences_.hdri_backdrop_visible;
    options.hdri_texture_path = hdri_relative_path(viewport_preferences_.active_hdri_environment_id);
    return render::make_plant_preview_stage_projection(*architecture, options);
}

Result<render::GrowthPreviewStageProjection>
ApplicationController::active_preview_stage_projection(render::GrowthPreviewStageOptions options) const
{
    if (active_workspace_ == "plant") {
        return plant_preview_stage_projection(options);
    }
    return growth_preview_stage_projection(options);
}

Result<void> ApplicationController::set_plant_physiological_age(float plant_physiological_age)
{
    if (!std::isfinite(plant_physiological_age) || plant_physiological_age < 0.0F) {
        return std::unexpected(make_error(ApplicationError::Code::InvalidCommand,
                                          "plant physiological age must be finite and non-negative"));
    }
    const auto* active_plant = project::active_plant_type(project_);
    const float max_age = active_plant != nullptr ? active_plant->parameters.plant_max_age : plant_physiological_age;
    plant_physiological_age_ = std::clamp(plant_physiological_age, 0.0F, max_age);
    return {};
}

Result<void> ApplicationController::set_active_workspace(std::string_view workspace)
{
    if (workspace != "module" && workspace != "plant" && workspace != "ecosystem") {
        return std::unexpected(
            make_error(ApplicationError::Code::InvalidCommand, "unknown workspace " + std::string(workspace)));
    }
    active_workspace_ = std::string(workspace);
    return {};
}

Result<project::PlantType> ApplicationController::plant_type(std::string_view plant_type_id) const
{
    const auto* found = project::plant_type_by_id(project_, plant_type_id);
    if (found == nullptr) {
        return std::unexpected(
            make_error(ApplicationError::Code::NotFound, "unknown plant type id " + std::string(plant_type_id)));
    }
    return *found;
}

Result<void> ApplicationController::set_active_prototype(std::size_t prototype_id)
{
    if (prototype_by_id(prototype_library_, prototype_id) == nullptr) {
        return std::unexpected(make_error(ApplicationError::Code::NotFound,
                                          "unknown branch module prototype id " + std::to_string(prototype_id)));
    }
    project_.active_branch_module_prototype_id = prototype_id;
    auto clamped = clamp_module_age_to_active_range();
    if (!clamped) {
        return std::unexpected(clamped.error());
    }
    return save_project();
}

Result<void> ApplicationController::set_active_plant_type(std::string_view plant_type_id)
{
    if (project::plant_type_by_id(project_, plant_type_id) == nullptr) {
        return std::unexpected(
            make_error(ApplicationError::Code::NotFound, "unknown plant type id " + std::string(plant_type_id)));
    }
    project_.plant_type_library.active_plant_type_id = std::string(plant_type_id);
    auto clamped = clamp_module_age_to_active_range();
    if (!clamped) {
        return std::unexpected(clamped.error());
    }
    return save_project();
}

Result<void> ApplicationController::set_module_physiological_age(float module_physiological_age)
{
    if (!std::isfinite(module_physiological_age) || module_physiological_age < 0.0F) {
        return std::unexpected(make_error(ApplicationError::Code::InvalidCommand,
                                          "module physiological age must be finite and non-negative"));
    }
    auto facts = module_workspace_facts(prototype_library_, project_);
    if (!facts) {
        return std::unexpected(facts.error());
    }
    module_physiological_age_ = std::min(module_physiological_age, facts->fully_grown_age);
    return {};
}

Result<project::PlantType> ApplicationController::create_plant_type(std::string name, char preset_key)
{
    const std::string id = project::next_plant_type_id(project_.plant_type_library);
    auto plant_type = project::create_plant_type_from_preset(id, std::move(name), preset_key);
    if (!plant_type) {
        return std::unexpected(from_project_error(plant_type.error()));
    }
    project_.plant_type_library.plant_types.push_back(*plant_type);
    auto saved = save_project();
    if (!saved) {
        return std::unexpected(saved.error());
    }
    return *plant_type;
}

Result<void> ApplicationController::delete_plant_type(std::string_view plant_type_id)
{
    auto deleted = project::delete_plant_type(project_, plant_type_id);
    if (!deleted) {
        return std::unexpected(from_project_error(deleted.error()));
    }
    auto clamped = clamp_module_age_to_active_range();
    if (!clamped) {
        return std::unexpected(clamped.error());
    }
    return save_project();
}

Result<void> ApplicationController::update_plant_type(project::PlantType plant_type)
{
    if (!growth::plant_type_parameters_are_valid(plant_type.parameters)) {
        return std::unexpected(make_error(ApplicationError::Code::InvalidCommand, "plant type parameters are invalid"));
    }
    auto* existing = project::plant_type_by_id(project_, plant_type.id);
    if (existing == nullptr) {
        return std::unexpected(make_error(ApplicationError::Code::NotFound, "unknown plant type id " + plant_type.id));
    }
    *existing = std::move(plant_type);
    auto clamped = clamp_module_age_to_active_range();
    if (!clamped) {
        return std::unexpected(clamped.error());
    }
    return save_project();
}

ViewportPreferences ApplicationController::viewport_preferences() const
{
    return viewport_preferences_;
}

std::vector<HdriEnvironment> ApplicationController::hdri_environments() const
{
    return enumerate_hdri_environments(options_.asset_root_path);
}

Result<void> ApplicationController::update_viewport_preferences(ViewportPreferences preferences)
{
    const auto environments = hdri_environments();
    if (!std::ranges::any_of(environments,
                             [&](const auto& environment) { return environment.id == preferences.active_hdri_environment_id; })) {
        return std::unexpected(make_error(ApplicationError::Code::NotFound, "unknown HDRI environment"));
    }
    viewport_preferences_ = std::move(preferences);
    save_viewport_preferences(options_.project_path, viewport_preferences_);
    return {};
}

Result<void> ApplicationController::clamp_module_age_to_active_range()
{
    auto facts = module_workspace_facts(prototype_library_, project_);
    if (!facts) {
        return std::unexpected(facts.error());
    }
    module_physiological_age_ = std::min(module_physiological_age_, facts->fully_grown_age);
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
    return "unknown";
}

} // namespace toi::app
