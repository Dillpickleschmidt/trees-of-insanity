#include "toi/ovrtx/environment.hpp"

#include <cstdlib>
#include <filesystem>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

namespace toi::ovrtx {
namespace {

void set_environment_variable(const char* name, const std::string& value, bool overwrite)
{
#ifdef _WIN32
    if (!overwrite && std::getenv(name) != nullptr) {
        return;
    }
    _putenv_s(name, value.c_str());
#else
    setenv(name, value.c_str(), overwrite ? 1 : 0);
#endif
}

} // namespace

void ensure_ovrtx_environment()
{
    set_environment_variable("OVRTX_SKIP_USD_CHECK", "1", false);
}

void prepend_usd_asset_search_path(const std::filesystem::path& path)
{
    if (path.empty()) {
        return;
    }

    const auto search_path = std::filesystem::absolute(path).lexically_normal().string();
    const char* current = std::getenv("PXR_AR_DEFAULT_SEARCH_PATH");
    if (current == nullptr || std::string(current).empty()) {
        set_environment_variable("PXR_AR_DEFAULT_SEARCH_PATH", search_path, true);
        return;
    }

#ifdef _WIN32
    constexpr char kSeparator = ';';
#else
    constexpr char kSeparator = ':';
#endif
    set_environment_variable("PXR_AR_DEFAULT_SEARCH_PATH", search_path + kSeparator + current, true);
}

} // namespace toi::ovrtx
