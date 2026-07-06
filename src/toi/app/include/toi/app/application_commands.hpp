#pragma once

#include "toi/app/application_controller.hpp"

#include <string_view>

#include <nlohmann/json.hpp>

namespace toi::app {

[[nodiscard]] bool application_command_changes_preview(std::string_view method);
[[nodiscard]] nlohmann::json handle_application_command(ApplicationController& controller,
                                                        const nlohmann::json& request);

} // namespace toi::app
