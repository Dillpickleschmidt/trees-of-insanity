#pragma once

#include "toi/growth/growth.hpp"

#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace toi::import {

struct ImportError {
    enum class Code {
        Io,
        Parse,
        UnknownObject,
        InvalidInput,
        InvalidPrototype,
    };

    Code code = Code::Parse;
    std::string message;
};

template <class T> using Result = std::expected<T, ImportError>;

using BranchModulePrototypeLibrary = growth::BranchModulePrototypeLibrary;

[[nodiscard]] Result<growth::BranchModulePrototype>
load_branch_module_prototype_from_obj(const std::filesystem::path& path, std::string_view object_name,
                                      std::size_t prototype_id, float prototype_geometry_scale);

[[nodiscard]] Result<BranchModulePrototypeLibrary>
load_branch_module_prototype_library_from_obj(const std::filesystem::path& path, float prototype_geometry_scale);

[[nodiscard]] std::optional<std::size_t> prototype_id_by_name(const BranchModulePrototypeLibrary& library,
                                                              std::string_view name);

} // namespace toi::import
