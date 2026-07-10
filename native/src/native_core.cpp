#include "toi/native/native_core.h"

#include "toi/app/application_commands.hpp"
#include "toi/app/application_controller.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <expected>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include <nlohmann/json.hpp>

#ifdef TOI_ENABLE_VIEWPORT
#include "toi/viewport/viewport_session.hpp"
#endif

struct ToiNativeCore {
    explicit ToiNativeCore(toi::app::ApplicationController controller)
        : controller(std::move(controller))
    {
    }

    toi::app::ApplicationController controller;
#ifdef TOI_ENABLE_VIEWPORT
    std::unique_ptr<toi::viewport::ViewportSession> viewport;
    int viewport_width = 0;
    int viewport_height = 0;
#endif
};

namespace {

using nlohmann::json;

thread_local std::string last_error = json{{"ok", false}, {"error", "no error"}}.dump();

[[nodiscard]] std::string application_error_code(toi::app::ApplicationError::Code code)
{
    switch (code) {
    case toi::app::ApplicationError::Code::Import:
        return "import";
    case toi::app::ApplicationError::Code::Project:
        return "project";
    case toi::app::ApplicationError::Code::Growth:
        return "growth";
    case toi::app::ApplicationError::Code::InvalidCommand:
        return "invalid_command";
    case toi::app::ApplicationError::Code::NotFound:
        return "not_found";
    }
    return "unknown";
}

void set_last_error(std::string message)
{
    last_error = json{{"ok", false}, {"error", std::move(message)}}.dump();
}

void set_last_error(const toi::app::ApplicationError& error)
{
    last_error = json{{"ok", false}, {"code", application_error_code(error.code)}, {"error", error.message}}.dump();
}

[[nodiscard]] char* copy_json_string(std::string_view value)
{
    auto* result = static_cast<char*>(std::malloc(value.size() + 1));
    if (result == nullptr) {
        set_last_error("failed to allocate result string");
        return nullptr;
    }
    std::memcpy(result, value.data(), value.size());
    result[value.size()] = '\0';
    return result;
}

[[nodiscard]] std::filesystem::path optional_path(const json& value, std::string_view key,
                                                  std::filesystem::path fallback)
{
    if (!value.contains(key)) {
        return fallback;
    }
    if (!value.at(key).is_string()) {
        throw std::invalid_argument(std::string(key) + " must be a string path");
    }
    return value.at(key).get<std::string>();
}

[[nodiscard]] toi::app::ApplicationControllerOptions parse_options(const char* options_json)
{
    toi::app::ApplicationControllerOptions options;
    if (options_json == nullptr || std::string_view(options_json).empty()) {
        return options;
    }

    const json value = json::parse(options_json);
    if (!value.is_object()) {
        throw std::invalid_argument("native core options must be an object");
    }

    options.project_path = optional_path(value, "project_path", options.project_path);
    options.asset_root_path = optional_path(value, "asset_root_path", options.asset_root_path);
    options.prototype_asset_path = optional_path(value, "prototype_asset_path", options.prototype_asset_path);
    return options;
}

[[nodiscard]] std::string command_error_response(std::string message)
{
    return json{{"id", nullptr}, {"ok", false}, {"error", std::move(message)}}.dump();
}

#ifdef TOI_ENABLE_OVRTX
// Rebuild the Growth preview at the native viewport's physical pixel size and
// hand it to the render thread. The command thread remains the sole controller
// owner; the render thread only consumes immutable stage projections.
[[nodiscard]] std::expected<void, std::string> push_growth_stage(ToiNativeCore* core)
{
    if (!core->viewport) {
        return {};
    }
    auto stage = core->controller.active_preview_stage_projection({
        .width = std::max(1, core->viewport_width),
        .height = std::max(1, core->viewport_height),
    });
    if (!stage) {
        return std::unexpected(stage.error().message);
    }
    core->viewport->set_pending_stage(std::move(*stage));
    const auto preferences = core->controller.viewport_preferences();
    core->viewport->set_guide_options(preferences.guides_visible, preferences.world_origin_axes_visible);
    return {};
}
#endif

} // namespace

extern "C" ToiNativeCore* toi_create(const char* options_json)
{
    try {
        auto controller = toi::app::ApplicationController::create(parse_options(options_json));
        if (!controller) {
            set_last_error(controller.error());
            return nullptr;
        }
        last_error = json{{"ok", true}}.dump();
        return new ToiNativeCore(std::move(*controller));
    } catch (const std::exception& error) {
        set_last_error(error.what());
        return nullptr;
    }
}

extern "C" void toi_destroy(ToiNativeCore* core)
{
    delete core;
}

extern "C" char* toi_handle_command(ToiNativeCore* core, const char* request_json)
{
    try {
        if (core == nullptr) {
            return copy_json_string(command_error_response("native core handle is null"));
        }
        if (request_json == nullptr) {
            return copy_json_string(command_error_response("request_json is null"));
        }

        const json request = json::parse(request_json);
        auto response = toi::app::handle_application_command(core->controller, request);
#ifdef TOI_ENABLE_OVRTX
        if (request.contains("method") && request.at("method").is_string() &&
            toi::app::application_command_changes_preview(request.at("method").get<std::string>())) {
            if (auto pushed = push_growth_stage(core); !pushed) {
                return copy_json_string(command_error_response(pushed.error()));
            }
        }
#endif
        return copy_json_string(response.dump());
    } catch (const std::exception& error) {
        return copy_json_string(command_error_response(error.what()));
    }
}

extern "C" char* toi_last_error_json()
{
    return copy_json_string(last_error);
}

extern "C" void toi_free_string(char* value)
{
    std::free(value);
}

extern "C" char* toi_attach_x11_viewport(ToiNativeCore* core, unsigned long x_window, int width, int height)
{
    if (core == nullptr) {
        return copy_json_string(json{{"ok", false}, {"error", "native core handle is null"}}.dump());
    }
#ifdef TOI_ENABLE_VIEWPORT
    try {
        auto session = toi::viewport::ViewportSession::attach(x_window, width, height);
        if (!session) {
            return copy_json_string(json{{"ok", false}, {"error", session.error().message}}.dump());
        }
        core->viewport = std::move(*session);
        const auto& info = core->viewport->info();
        core->viewport_width = info.width;
        core->viewport_height = info.height;
#ifdef TOI_ENABLE_OVRTX
        if (auto pushed = push_growth_stage(core); !pushed) {
            core->viewport.reset();
            core->viewport_width = 0;
            core->viewport_height = 0;
            return copy_json_string(json{{"ok", false}, {"error", pushed.error()}}.dump());
        }
#endif
        return copy_json_string(json{{"ok", true},
                                     {"device", info.device_name},
                                     {"width", info.width},
                                     {"height", info.height}}
                                    .dump());
    } catch (const std::exception& error) {
        return copy_json_string(json{{"ok", false}, {"error", error.what()}}.dump());
    }
#else
    (void)x_window;
    (void)width;
    (void)height;
    return copy_json_string(json{{"ok", false}, {"error", "viewport support not built in this core"}}.dump());
#endif
}

extern "C" char* toi_resize_x11_viewport(ToiNativeCore* core, int width, int height)
{
    if (core == nullptr) {
        return copy_json_string(json{{"ok", false}, {"error", "native core handle is null"}}.dump());
    }
#ifdef TOI_ENABLE_VIEWPORT
    if (!core->viewport) {
        return copy_json_string(json{{"ok", false}, {"error", "native viewport is not attached"}}.dump());
    }
    if (width <= 0 || height <= 0) {
        return copy_json_string(json{{"ok", false}, {"error", "native viewport size must be positive"}}.dump());
    }
    if (width == core->viewport_width && height == core->viewport_height) {
        return copy_json_string(json{{"ok", true}, {"changed", false}, {"width", width}, {"height", height}}.dump());
    }

    const int previous_width = core->viewport_width;
    const int previous_height = core->viewport_height;
    core->viewport_width = width;
    core->viewport_height = height;
#ifdef TOI_ENABLE_OVRTX
    if (auto pushed = push_growth_stage(core); !pushed) {
        core->viewport_width = previous_width;
        core->viewport_height = previous_height;
        return copy_json_string(json{{"ok", false}, {"error", pushed.error()}}.dump());
    }
#endif
    return copy_json_string(json{{"ok", true}, {"changed", true}, {"width", width}, {"height", height}}.dump());
#else
    (void)width;
    (void)height;
    return copy_json_string(json{{"ok", false}, {"error", "viewport support not built in this core"}}.dump());
#endif
}

extern "C" char* toi_detach_viewport(ToiNativeCore* core)
{
    if (core == nullptr) {
        return copy_json_string(json{{"ok", false}, {"error", "native core handle is null"}}.dump());
    }
#ifdef TOI_ENABLE_VIEWPORT
    core->viewport.reset();
    core->viewport_width = 0;
    core->viewport_height = 0;
    return copy_json_string(json{{"ok", true}}.dump());
#else
    return copy_json_string(json{{"ok", false}, {"error", "viewport support not built in this core"}}.dump());
#endif
}
