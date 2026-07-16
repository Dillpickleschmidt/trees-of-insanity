#pragma once

#include "toi/model/desktop_session.hpp"

#include <string>
#include <string_view>

namespace toi::desktop {

struct ActionDispatchResult {
    std::string response;
    bool preview_changed = false;
};

[[nodiscard]] ActionDispatchResult dispatch_action(model::DesktopSession& session, std::string_view request);

} // namespace toi::desktop
