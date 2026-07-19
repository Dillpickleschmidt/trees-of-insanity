#pragma once

#include "toi/model/desktop_session.hpp"

#include <optional>
#include <string>
#include <string_view>

namespace toi::desktop {

enum class PlantRunControl {
    None,
    Start,
    Stop,
};

struct ActionDispatchResult {
    std::string response;
    bool preview_changed = false;
    PlantRunControl plant_run_control = PlantRunControl::None;
    std::optional<double> viewport_frames_per_second;
};

[[nodiscard]] ActionDispatchResult dispatch_action(model::DesktopSession& session, std::string_view request);

} // namespace toi::desktop
