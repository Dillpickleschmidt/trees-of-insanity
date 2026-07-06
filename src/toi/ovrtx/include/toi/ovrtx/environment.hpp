#pragma once

#include <filesystem>

namespace toi::ovrtx {

void ensure_ovrtx_environment();
void prepend_usd_asset_search_path(const std::filesystem::path& path);

} // namespace toi::ovrtx
