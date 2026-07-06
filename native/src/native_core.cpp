#include "toi/native/native_core.h"

#include "toi/app/application_commands.hpp"
#include "toi/app/application_controller.hpp"

#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include <nlohmann/json.hpp>

struct ToiNativeCore {
    explicit ToiNativeCore(toi::app::ApplicationController controller)
        : controller(std::move(controller))
    {
    }

    toi::app::ApplicationController controller;
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
        return copy_json_string(toi::app::handle_application_command(core->controller, request).dump());
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
