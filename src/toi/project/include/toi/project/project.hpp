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

struct PlantType {
    std::string id;
    std::string name;
    growth::PlantTypeParameters parameters;
};

struct PlantTypeLibrary {
    std::vector<PlantType> plant_types;
    std::string active_plant_type_id;
};

struct Project {
    int version = 1;
    PlantTypeLibrary plant_type_library;
    std::size_t active_branch_module_prototype_id = 0;
};

[[nodiscard]] Result<Project> make_default_project(std::size_t default_prototype_id);
[[nodiscard]] Result<Project> load_project(const std::filesystem::path& path);
[[nodiscard]] Result<void> save_project(const std::filesystem::path& path, const Project& project);
[[nodiscard]] Result<Project> load_or_create_project(const std::filesystem::path& path,
                                                     std::size_t default_prototype_id);

[[nodiscard]] const PlantType* active_plant_type(const Project& project);
[[nodiscard]] const PlantType* plant_type_by_id(const Project& project, std::string_view id);
[[nodiscard]] PlantType* plant_type_by_id(Project& project, std::string_view id);

[[nodiscard]] Result<PlantType> create_plant_type_from_preset(std::string id, std::string name, char preset_key);

[[nodiscard]] std::string next_plant_type_id(const PlantTypeLibrary& library);
[[nodiscard]] Result<void> delete_plant_type(Project& project, std::string_view id);
[[nodiscard]] Result<void> require_valid_project(const Project& project);

} // namespace toi::project
