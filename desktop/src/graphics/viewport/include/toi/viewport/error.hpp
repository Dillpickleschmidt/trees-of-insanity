#pragma once

#include <expected>
#include <string>

namespace toi::viewport {

struct Error {
    std::string message;
};

template <class T> using Result = std::expected<T, Error>;

[[nodiscard]] Error make_error(std::string message);

} // namespace toi::viewport
