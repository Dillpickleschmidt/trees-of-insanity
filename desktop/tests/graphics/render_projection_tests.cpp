#include "toi/growth/growth.hpp"
#include "toi/import/obj_importer.hpp"
#include "toi/render/render_projection.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>

namespace {

std::filesystem::path prototype_path()
{
    return TOI_TEST_PROTOTYPE_ASSET_PATH;
}

toi::import::BranchModulePrototypeLibrary load_library()
{
    auto library = toi::import::load_branch_module_prototype_library_from_obj(prototype_path());
    REQUIRE(library.has_value());
    return std::move(*library);
}

toi::growth::PlantArchitecture develop(char preset_key, float age)
{
    const auto library = load_library();
    const auto plant_type = toi::growth::plant_type_preset_by_key(preset_key);
    REQUIRE(plant_type.has_value());
    auto architecture = toi::growth::develop_plant(*plant_type, library, age);
    REQUIRE(architecture.has_value());
    return std::move(*architecture);
}

} // namespace

TEST_CASE("plant preview projects a multi-module architecture into welded chains")
{
    const auto architecture = develop('i', 250.0F);
    REQUIRE(architecture.modules.size() > 1);

    const auto projection = toi::render::make_plant_preview_stage_projection(architecture, {.width = 640, .height = 480});

    REQUIRE(projection.mesh.mesh_count > 0);
    REQUIRE(projection.mesh.chain_count > 1); // multiple modules -> multiple chains
    REQUIRE(projection.mesh.vertex_count > 0);
    REQUIRE(projection.mesh_attributes.size() == projection.mesh.mesh_count);
    REQUIRE_FALSE(projection.usd_stage.text.empty());
    REQUIRE(projection.usd_stage.text.find("def Mesh") != std::string::npos);

    // Camera frames a non-degenerate region around the whole plant.
    REQUIRE(projection.camera.width == 640);
    REQUIRE(projection.camera.height == 480);
    REQUIRE(toi::growth::distance(projection.camera.eye, projection.camera.target) > 0.0F);
}

TEST_CASE("plant preview rendered chains track the module set as it grows")
{
    const auto young = develop('i', 60.0F);
    const auto old = develop('i', 250.0F);
    REQUIRE(old.modules.size() >= young.modules.size());

    const auto young_projection = toi::render::make_plant_preview_stage_projection(young);
    const auto old_projection = toi::render::make_plant_preview_stage_projection(old);
    REQUIRE(old_projection.mesh.chain_count >= young_projection.mesh.chain_count);
}

TEST_CASE("plant preview projection is deterministic")
{
    const auto architecture = develop('g', 150.0F);
    const auto first = toi::render::make_plant_preview_stage_projection(architecture);
    const auto second = toi::render::make_plant_preview_stage_projection(architecture);
    REQUIRE(first.mesh.vertex_count == second.mesh.vertex_count);
    REQUIRE(first.mesh.chain_count == second.mesh.chain_count);
    REQUIRE(first.usd_stage.text == second.usd_stage.text);
}
