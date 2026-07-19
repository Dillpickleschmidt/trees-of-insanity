#include "desktop_actions.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <nlohmann/json.hpp>

TEST_CASE("raw desktop actions report response and preview effect")
{
    const auto project_path = std::filesystem::temp_directory_path() / "toi-desktop-actions-test.project.json";
    std::filesystem::remove(project_path);
    std::filesystem::remove(project_path.string() + ".tmp");
    auto session = toi::model::DesktopSession::create({
        .project_path = project_path,
        .asset_root_path = std::filesystem::path(TOI_TEST_PROTOTYPE_ASSET_PATH).parent_path().parent_path(),
        .prototype_asset_path = TOI_TEST_PROTOTYPE_ASSET_PATH,
    });
    REQUIRE(session);

    auto read = toi::desktop::dispatch_action(*session, R"({"method":"app.get_state"})");
    CHECK(nlohmann::json::parse(read.response).at("ok") == true);
    CHECK_FALSE(read.preview_changed);

    auto visual = toi::desktop::dispatch_action(*session, R"({"method":"module.set_age","params":{"age":1}})");
    CHECK(nlohmann::json::parse(visual.response).at("ok") == true);
    CHECK(visual.preview_changed);

    auto nonvisual = toi::desktop::dispatch_action(
        *session, R"({"method":"plant.set_run_settings","params":{"target_age":250,"step_size":0.5}})");
    CHECK(nlohmann::json::parse(nonvisual.response).at("ok") == true);
    CHECK_FALSE(nonvisual.preview_changed);

    auto run = toi::desktop::dispatch_action(*session, R"({"method":"plant.run"})");
    CHECK(nlohmann::json::parse(run.response).at("ok") == true);
    CHECK(run.plant_run_control == toi::desktop::PlantRunControl::Start);
    auto stop = toi::desktop::dispatch_action(*session, R"({"method":"plant.stop"})");
    CHECK(nlohmann::json::parse(stop.response).at("ok") == true);
    CHECK(stop.plant_run_control == toi::desktop::PlantRunControl::Stop);

    auto frames_per_second = toi::desktop::dispatch_action(
        *session, R"({"method":"viewport.set_frames_per_second","params":{"frames_per_second":60}})");
    CHECK(nlohmann::json::parse(frames_per_second.response).at("ok") == true);
    CHECK(frames_per_second.viewport_frames_per_second == 60.0);
    auto invalid_frames_per_second = toi::desktop::dispatch_action(
        *session, R"({"method":"viewport.set_frames_per_second","params":{"frames_per_second":0}})");
    CHECK(nlohmann::json::parse(invalid_frames_per_second.response).at("ok") == false);
    CHECK_FALSE(invalid_frames_per_second.viewport_frames_per_second);

    const auto before_invalid_prototype = session->state();
    REQUIRE(before_invalid_prototype);
    auto invalid_prototype = toi::desktop::dispatch_action(
        *session, R"({"method":"module.set_active_prototype","params":{"prototype_id":7.9}})");
    CHECK(nlohmann::json::parse(invalid_prototype.response).at("ok") == false);
    CHECK_FALSE(invalid_prototype.preview_changed);
    const auto after_invalid_prototype = session->state();
    REQUIRE(after_invalid_prototype);
    CHECK(after_invalid_prototype->active_prototype_id == before_invalid_prototype->active_prototype_id);

    auto created = toi::desktop::dispatch_action(
        *session, R"({"method":"plant_types.create","params":{"name":"Test type","preset_key":"a"}})");
    const auto created_response = nlohmann::json::parse(created.response);
    CHECK(created_response.at("ok") == true);
    CHECK(created_response.at("result").size() == 2);
    CHECK(created_response.at("result").at("name") == "Test type");
    CHECK_FALSE(created.preview_changed);

    auto selected = toi::desktop::dispatch_action(*session, nlohmann::json{
        {"method", "module.set_active_plant_type"},
        {"params", {{"plant_type_id", created_response.at("result").at("id")}}},
    }.dump());
    CHECK(nlohmann::json::parse(selected.response).at("ok") == true);
    CHECK(selected.preview_changed);

    auto error = toi::desktop::dispatch_action(*session, R"({"method":"module.set_age","params":{"age":-1}})");
    CHECK(nlohmann::json::parse(error.response).at("ok") == false);
    CHECK_FALSE(error.preview_changed);
    REQUIRE(session->state());
    CHECK(session->state()->module_physiological_age == 1.0F);

    auto malformed = toi::desktop::dispatch_action(*session, "{");
    CHECK(nlohmann::json::parse(malformed.response).at("ok") == false);
    CHECK_FALSE(malformed.preview_changed);
}
