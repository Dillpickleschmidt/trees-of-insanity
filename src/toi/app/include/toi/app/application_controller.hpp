#pragma once

#include "toi/growth/growth.hpp"
#include "toi/import/obj_importer.hpp"
#include "toi/project/project.hpp"
#include "toi/render/render_projection.hpp"

#include <expected>
#include <filesystem>
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

    [[nodiscard]] Result<project::PlantType> create_plant_type(std::string name, char preset_key);
    [[nodiscard]] Result<void> delete_plant_type(std::string_view plant_type_id);
    [[nodiscard]] Result<void> update_plant_type(project::PlantType plant_type);

private:
    ApplicationController(ApplicationControllerOptions options, import::BranchModulePrototypeLibrary prototype_library,
                          project::Project project, float module_physiological_age);

    [[nodiscard]] Result<void> clamp_module_age_to_active_range();

    ApplicationControllerOptions options_;
    import::BranchModulePrototypeLibrary prototype_library_;
    project::Project project_;
    float module_physiological_age_ = 0.0F;
};

[[nodiscard]] std::string to_string(PrototypeTreeItem::Kind kind);

} // namespace toi::app
