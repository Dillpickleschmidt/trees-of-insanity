#pragma once

#include <ovrtx/ovrtx_types.h>

#include <expected>
#include <string>
#include <string_view>

namespace toi::ovrtx {

struct Error {
    std::string message;
};

template <class T> using Result = std::expected<T, Error>;

[[nodiscard]] Error make_error(std::string message);
[[nodiscard]] std::string from_ovx_string(ovx_string_t value);
[[nodiscard]] ovx_string_t to_ovx_string(std::string_view value);
[[nodiscard]] std::string last_error_message(std::string_view context);

} // namespace toi::ovrtx
