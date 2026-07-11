#include "toi/model/desktop_session.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>
#include <utility>

namespace {

std::filesystem::path fresh_project_path(const std::string& name)
{
    const auto root = std::filesystem::temp_directory_path() / "trees-of-insanity-tests";
    std::filesystem::create_directories(root);
    auto path = root / (name + ".toi.project.json");
    std::filesystem::remove(path);
    return path;
}

toi::model::DesktopSession make_session(const std::string& name)
{
    toi::model::DesktopSessionOptions options;
    options.project_path = fresh_project_path(name);
    options.prototype_asset_path = TOI_TEST_PROTOTYPE_ASSET_PATH;
    auto session = toi::model::DesktopSession::create(std::move(options));
    REQUIRE(session.has_value());
    return std::move(*session);
}

} // namespace

TEST_CASE("plant workspace is unlocked and switchable")
{
    auto session = make_session("plant-workspace");
    const auto initial = session.state();
    REQUIRE(initial.has_value());

    bool plant_implemented = false;
    for (const auto& preview : initial->workspace_previews) {
        if (preview.workspace == "plant") {
            plant_implemented = preview.implemented;
        }
    }
    REQUIRE(plant_implemented);

    REQUIRE(session.set_active_workspace("plant").has_value());
    const auto after = session.state();
    REQUIRE(after.has_value());
    REQUIRE(after->active_workspace == "plant");
    REQUIRE_FALSE(session.set_active_workspace("bogus").has_value());
}

TEST_CASE("plant age drives the plant growth summary and clamps to max")
{
    auto session = make_session("plant-age");
    const auto state = session.state();
    REQUIRE(state.has_value());
    const float max_age = state->plant_fully_grown_age;
    REQUIRE(max_age > 0.0F);

    REQUIRE(session.set_plant_physiological_age(max_age).has_value());
    const auto summary = session.plant_growth_summary();
    REQUIRE(summary.has_value());
    REQUIRE(summary->module_count >= 1);
    REQUIRE(summary->plant_fully_grown_age == max_age);

    // Over-max requests clamp to the plant's max age.
    REQUIRE(session.set_plant_physiological_age(max_age * 10.0F).has_value());
    const auto clamped = session.state();
    REQUIRE(clamped.has_value());
    REQUIRE(clamped->plant_physiological_age <= max_age + 1.0e-3F);
}

TEST_CASE("active preview dispatches by workspace")
{
    auto session = make_session("plant-preview");
    const auto state = session.state();
    REQUIRE(state.has_value());
    REQUIRE(session.set_plant_physiological_age(state->plant_fully_grown_age).has_value());

    REQUIRE(session.set_active_workspace("plant").has_value());
    const auto plant = session.active_preview_snapshot();
    REQUIRE(plant.has_value());
    REQUIRE(std::holds_alternative<toi::growth::PlantArchitecture>(*plant));

    REQUIRE(session.set_active_workspace("module").has_value());
    const auto module = session.active_preview_snapshot();
    REQUIRE(module.has_value());
    REQUIRE(std::holds_alternative<toi::model::ModulePreviewSnapshot>(*module));
}

TEST_CASE("switching the active plant type re-develops the plant state")
{
    auto session = make_session("plant-switch");

    // Create and activate a plant type from a short-lived preset ('a', plant_max_age 20).
    const auto created = session.create_plant_type("Tiny", 'a');
    REQUIRE(created.has_value());
    REQUIRE(session.set_active_plant_type(created->id).has_value());

    const auto state = session.state();
    REQUIRE(state.has_value());
    REQUIRE(state->plant_fully_grown_age == 20.0F);
}

TEST_CASE("active preview rejects the unimplemented ecosystem workspace")
{
    auto session = make_session("plant-ecosystem");
    REQUIRE(session.set_active_workspace("ecosystem").has_value());
    REQUIRE_FALSE(session.active_preview_snapshot().has_value());
}
