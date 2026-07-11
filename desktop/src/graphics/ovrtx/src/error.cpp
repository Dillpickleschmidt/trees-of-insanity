#include "toi/ovrtx/error.hpp"

#include <ovrtx/ovrtx.h>

#include <string>
#include <string_view>
#include <utility>

namespace toi::ovrtx {

Error make_error(std::string message)
{
    return {.message = std::move(message)};
}

std::string from_ovx_string(ovx_string_t value)
{
    if (value.ptr == nullptr || value.length == 0) {
        return {};
    }
    return {value.ptr, value.length};
}

ovx_string_t to_ovx_string(std::string_view value)
{
    return {.ptr = value.data(), .length = value.size()};
}

std::string last_error_message(std::string_view context)
{
    const auto error = from_ovx_string(ovrtx_get_last_error());
    if (error.empty()) {
        return std::string(context);
    }
    return std::string(context) + ": " + error;
}

} // namespace toi::ovrtx
