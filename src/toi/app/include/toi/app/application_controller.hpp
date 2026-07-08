#pragma once

#include "toi/growth/growth.hpp"
#include "toi/import/obj_importer.hpp"
#include "toi/plant/plant.hpp"
#include "toi/project/project.hpp"
#include "toi/render/render_projection.hpp"

#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace toi::app {

struct ApplicationError {
    enum class Code {
        Import,
        Project,
        Growth,
        InvalidCommand,
        NotFound,
    };

    Code code = Code::InvalidCommand;
    std::string message;
};

template <class T> using Result = std::expected<T, ApplicationError>;

struct ApplicationControllerOptions {
    std::filesystem::path project_path = "toi.project.json";
    std::filesystem::path asset_root_path = TOI_DEFAULT_ASSET_ROOT_PATH;
    std::filesystem::path prototype_asset_path = TOI_DEFAULT_PROTOTYPE_ASSET_PATH;
};

struct PrototypeSummary {
    std::size_t id = 0;
    std::string name;
    std::size_t node_count = 0;
    std::size_t segment_count = 0;
};

struct PlantTypeSummary {
    std::string id;
    std::string name;
};

struct WorkspacePreviewState {
    std::string workspace;
    bool implemented = false;
};

struct AppStateView {
    std::string active_workspace = "module";
    std::vector<WorkspacePreviewState> workspace_previews;
    std::vector<PrototypeSummary> prototypes;
    std::size_t active_prototype_id = 0;
    std::vector<PlantTypeSummary> plant_types;
    std::string active_plant_type_id;
    float module_physiological_age = 0.0F;
    float fully_grown_age = 0.0F;
    float plant_physiological_age = 0.0F;
    float plant_fully_grown_age = 0.0F;
};

struct PrototypeTreeItem {
    enum class Kind {
        Node,
        Segment,
    };

    Kind kind = Kind::Node;
    std::size_t id = 0;
    std::string label;
    std::vector<PrototypeTreeItem> children;
};

struct PrototypeTreeView {
    PrototypeTreeItem root;
};

struct GrowthSnapshotSummary {
    float module_physiological_age = 0.0F;
    float growth_rate = 0.0F;
    std::size_t visible_segment_count = 0;
    std::size_t growing_segment_count = 0;
    std::size_t mature_segment_count = 0;
    float max_diameter = 0.0F;
};

struct PlantGrowthSummary {
    float plant_physiological_age = 0.0F;
    float plant_fully_grown_age = 0.0F;
    std::size_t module_count = 0;
    std::size_t visible_segment_count = 0;
    float max_diameter = 0.0F;
    bool senescent = false;
};

struct HdriEnvironment {
    std::string id;
    std::string name;
    bool bundled = true;
};

struct ViewportPreferences {
    bool guides_visible = true;
    bool world_origin_axes_visible = true;
    bool hdri_backdrop_visible = true;
    std::string active_hdri_environment_id;
};

class ApplicationController {
public:
    [[nodiscard]] static Result<ApplicationController> create(ApplicationControllerOptions options = {});

    [[nodiscard]] Result<void> save_project() const;
    [[nodiscard]] Result<AppStateView> state() const;
    [[nodiscard]] Result<PrototypeTreeView> prototype_tree() const;
    [[nodiscard]] Result<growth::GrowthSnapshot> growth_snapshot() const;
    [[nodiscard]] Result<GrowthSnapshotSummary> growth_snapshot_summary() const;
    [[nodiscard]] Result<render::GrowthPreviewStageProjection>
    growth_preview_stage_projection(render::GrowthPreviewStageOptions options = {}) const;
    [[nodiscard]] Result<project::PlantType> plant_type(std::string_view plant_type_id) const;

    [[nodiscard]] Result<void> set_active_prototype(std::size_t prototype_id);
    [[nodiscard]] Result<void> set_active_plant_type(std::string_view plant_type_id);
    [[nodiscard]] Result<void> set_module_physiological_age(float module_physiological_age);

    // Plant workspace: develop the active plant type at the plant physiological age.
    [[nodiscard]] Result<plant::PlantArchitecture> plant_architecture() const;
    [[nodiscard]] Result<PlantGrowthSummary> plant_growth_summary() const;
    [[nodiscard]] Result<render::GrowthPreviewStageProjection>
    plant_preview_stage_projection(render::GrowthPreviewStageOptions options = {}) const;
    // Transient preview of a built-in plant type preset (species gallery); no state change.
    // A nullopt age previews the fully-grown plant; a provided age is validated and clamped.
    [[nodiscard]] Result<render::GrowthPreviewStageProjection>
    plant_preset_preview_stage_projection(char preset_key, std::optional<float> plant_age,
                                          render::GrowthPreviewStageOptions options = {}) const;
    // Dispatch to the module or plant preview based on the active workspace.
    [[nodiscard]] Result<render::GrowthPreviewStageProjection>
    active_preview_stage_projection(render::GrowthPreviewStageOptions options = {}) const;
    [[nodiscard]] Result<void> set_plant_physiological_age(float plant_physiological_age);
    [[nodiscard]] Result<void> set_active_workspace(std::string_view workspace);

    [[nodiscard]] Result<project::PlantType> create_plant_type(std::string name, char preset_key);
    [[nodiscard]] Result<void> delete_plant_type(std::string_view plant_type_id);
    [[nodiscard]] Result<void> update_plant_type(project::PlantType plant_type);

    [[nodiscard]] ViewportPreferences viewport_preferences() const;
    [[nodiscard]] std::vector<HdriEnvironment> hdri_environments() const;
    [[nodiscard]] Result<void> update_viewport_preferences(ViewportPreferences preferences);

private:
    ApplicationController(ApplicationControllerOptions options, import::BranchModulePrototypeLibrary prototype_library,
                          project::Project project, float module_physiological_age);

    [[nodiscard]] Result<void> clamp_module_age_to_active_range();

    ApplicationControllerOptions options_;
    import::BranchModulePrototypeLibrary prototype_library_;
    project::Project project_;
    float module_physiological_age_ = 0.0F;
    float plant_physiological_age_ = 0.0F;
    std::string active_workspace_ = "module";
    ViewportPreferences viewport_preferences_;
};

[[nodiscard]] std::string to_string(PrototypeTreeItem::Kind kind);

} // namespace toi::app
