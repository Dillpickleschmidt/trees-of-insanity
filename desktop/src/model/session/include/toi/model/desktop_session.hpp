#pragma once

#include "toi/growth/growth.hpp"
#include "toi/import/obj_importer.hpp"
#include "toi/project/project.hpp"

#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace toi::model {

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

struct DesktopSessionOptions {
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

struct HdriEnvironment {
    std::string id;
    std::string name;
    bool bundled = true;
};

struct ModulePreviewSnapshot {
    growth::GrowthSnapshot snapshot;
    growth::GrowthSnapshot camera_snapshot;
    growth::BranchModulePrototype prepared_prototype;
};

struct PlantStateView {
    float plant_age = 0.0F;
    float root_physiological_age = 0.0F;
    float root_fully_grown_age = 0.0F;
    float target_age = 500.0F;
    float step_size = 10.0F;
    std::size_t root_prototype_id = 0;
    std::string plant_type_id;
    bool module_diagnostic_labels_visible = false;
    bool direct_light_bounding_spheres_visible = false;
    bool module_accumulated_light_visible = false;
    bool module_vigor_visible = false;
    bool mature_terminal_markers_visible = false;
    float direct_light_exposure = 0.0F;
    float accumulated_light = 0.0F;
    float vigor = 0.0F;
    float growth_rate = 0.0F;
};

struct PlantAdvanceResult {
    bool reached_target = false;
};

struct PlantPreviewSnapshot {
    growth::PlantSnapshot snapshot;
    growth::GrowthSnapshot mature_root_snapshot;
    growth::BranchModulePrototype prepared_root;
};

struct PlantDiagnosticsUpdate {
    bool module_diagnostic_labels_visible = false;
    bool direct_light_bounding_spheres_visible = false;
    bool module_accumulated_light_visible = false;
    bool module_vigor_visible = false;
    bool mature_terminal_markers_visible = false;
};

struct PreviewEnvironment {
    std::filesystem::path asset_search_path;
    std::filesystem::path hdri_texture_path;
    bool hdri_visible = true;
    bool guides_visible = true;
    bool world_origin_axes_visible = true;
};

struct ViewportAppearance {
    bool guides_visible = true;
    bool world_origin_axes_visible = true;
    bool hdri_backdrop_visible = true;
    std::string active_hdri_environment_id;
};

class DesktopSession {
public:
    [[nodiscard]] static Result<DesktopSession> create(DesktopSessionOptions options = {});

    [[nodiscard]] Result<AppStateView> state() const;
    [[nodiscard]] Result<PrototypeTreeView> prototype_tree() const;
    [[nodiscard]] Result<growth::GrowthSnapshot> growth_snapshot() const;
    [[nodiscard]] Result<GrowthSnapshotSummary> growth_snapshot_summary() const;
    [[nodiscard]] Result<ModulePreviewSnapshot> module_preview_snapshot() const;
    [[nodiscard]] Result<PlantStateView> plant_state() const;
    [[nodiscard]] Result<PlantPreviewSnapshot> plant_preview_snapshot() const;
    [[nodiscard]] Result<project::PlantType> plant_type(std::string_view plant_type_id) const;

    [[nodiscard]] Result<void> set_active_prototype(std::size_t prototype_id);
    [[nodiscard]] Result<void> set_active_plant_type(std::string_view plant_type_id);
    [[nodiscard]] Result<void> set_module_physiological_age(float module_physiological_age);

    [[nodiscard]] PreviewEnvironment preview_environment() const;
    [[nodiscard]] Result<void> set_active_workspace(std::string_view workspace);
    [[nodiscard]] Result<PlantAdvanceResult> advance_plant();
    [[nodiscard]] Result<void> finish_plant_run();
    [[nodiscard]] Result<void> plant_reset();
    [[nodiscard]] Result<void> update_plant_run_settings(float target_age, float step_size);
    [[nodiscard]] Result<void> update_plant_diagnostics(PlantDiagnosticsUpdate diagnostics);

    [[nodiscard]] Result<project::PlantType> create_plant_type(std::string name, char preset_key);
    [[nodiscard]] Result<void> delete_plant_type(std::string_view plant_type_id);
    [[nodiscard]] Result<void> update_plant_type(project::PlantType plant_type);

    [[nodiscard]] ViewportAppearance viewport_preferences() const;
    [[nodiscard]] project::OrbitState active_orbit() const;
    [[nodiscard]] bool active_camera_needs_frame() const;
    [[nodiscard]] std::vector<HdriEnvironment> hdri_environments() const;
    [[nodiscard]] Result<void> update_viewport_preferences(ViewportAppearance appearance);
    [[nodiscard]] Result<void> update_active_orbit(project::OrbitState orbit);

private:
    DesktopSession(DesktopSessionOptions options, import::BranchModulePrototypeLibrary prototype_library,
                   project::Project project, growth::PlantSimulation plant_simulation);

    [[nodiscard]] Result<void> commit_project(project::Project project);
    [[nodiscard]] Result<void> clamp_module_age_to_active_range(project::Project& project) const;
    [[nodiscard]] Result<void> commit_plant_type_change(project::Project updated_project, bool module_selected,
                                                        bool plant_selected);

    DesktopSessionOptions options_;
    import::BranchModulePrototypeLibrary prototype_library_;
    project::Project project_;
    growth::PlantSimulation plant_simulation_;
    std::optional<float> plant_run_camera_target_z_;
    bool module_camera_needs_frame_ = true;
    bool plant_camera_needs_frame_ = true;
};

[[nodiscard]] std::string to_string(PrototypeTreeItem::Kind kind);

} // namespace toi::model
