#include "toi/viewport/error.hpp"

#include <utility>

namespace toi::viewport {

Error make_error(std::string message)
{
    return Error{.message = std::move(message)};
}

} // namespace toi::viewport
