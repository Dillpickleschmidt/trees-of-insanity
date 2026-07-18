#pragma once

#include "toi/growth/growth.hpp"

#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace toi::project {

struct ProjectError {
    enum class Code {
        Io,
        Parse,
        InvalidProject,
        UnknownPlantTypePreset,
    };

    Code code = Code::InvalidProject;
    std::string message;
};

template <class T> using Result = std::expected<T, ProjectError>;

enum class Workspace {
    Module,
    Plant,
    Ecosystem,
};

struct OrbitTarget {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
};

struct OrbitState {
    OrbitTarget target;
    float radius = 1.0F;
    float azimuth_radians = 0.0F;
    float elevation_radians = 0.0F;
};

struct ViewportState {
    bool guides_visible = true;
    bool world_origin_axes_visible = true;
    bool hdri_backdrop_visible = true;
    std::string active_hdri_environment_id;
    OrbitState orbit;
    bool orbit_initialized = false;
};

struct PlantType {
    std::string id;
    std::string name;
    growth::PlantTypeParameters parameters;
};

struct PlantTypeLibrary {
    std::vector<PlantType> plant_types;
};

struct ModuleWorkspaceState {
    std::size_t prototype_id = 0;
    std::string plant_type_id;
    float physiological_age = 0.0F;
    ViewportState viewport;
};

struct PlantDiagnostics {
    bool module_diagnostic_labels_visible = false;
    bool direct_light_bounding_spheres_visible = false;
    bool module_accumulated_light_visible = false;
    bool module_vigor_visible = false;
    bool mature_terminal_markers_visible = false;
};

struct PlantWorkspaceState {
    std::size_t root_prototype_id = 0;
    std::string plant_type_id;
    float simulation_timestep = 1.0F;
    PlantDiagnostics diagnostics;
    ViewportState viewport;
};

struct EcosystemWorkspaceState {
    ViewportState viewport;
};

struct Project {
    int version = 2;
    PlantTypeLibrary plant_type_library;
    Workspace active_workspace = Workspace::Module;
    ModuleWorkspaceState module_workspace;
    PlantWorkspaceState plant_workspace;
    EcosystemWorkspaceState ecosystem_workspace;
};

[[nodiscard]] Project make_default_project(std::size_t default_prototype_id,
                                           std::string default_hdri_environment_id);
[[nodiscard]] Result<Project> load_project(const std::filesystem::path& path);
[[nodiscard]] Result<void> save_project(const std::filesystem::path& path, const Project& project);

[[nodiscard]] const PlantType* plant_type_by_id(const Project& project, std::string_view id);
[[nodiscard]] PlantType* plant_type_by_id(Project& project, std::string_view id);
[[nodiscard]] Result<PlantType> create_plant_type_from_preset(std::string id, std::string name, char preset_key);
[[nodiscard]] std::string next_plant_type_id(const PlantTypeLibrary& library);
[[nodiscard]] Result<void> delete_plant_type(Project& project, std::string_view id);
[[nodiscard]] std::string_view to_string(Workspace workspace);

} // namespace toi::project
