#pragma once

#include "toi/model/desktop_session.hpp"

#include <string_view>

#include <nlohmann/json.hpp>

namespace toi::desktop {

[[nodiscard]] bool action_changes_preview(std::string_view method);
[[nodiscard]] nlohmann::json dispatch_action(model::DesktopSession& session,
                                                        const nlohmann::json& request);

} // namespace toi::desktop
