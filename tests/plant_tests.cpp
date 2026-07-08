#include "toi/growth/growth.hpp"
#include "toi/import/obj_importer.hpp"
#include "toi/plant/plant.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <filesystem>

namespace {

std::filesystem::path prototype_path()
{
    return TOI_TEST_PROTOTYPE_ASSET_PATH;
}

toi::import::BranchModulePrototypeLibrary load_library()
{
    auto library = toi::import::load_branch_module_prototype_library_from_obj(prototype_path());
    REQUIRE(library.has_value());
    REQUIRE_FALSE(library->prototypes.empty());
    return std::move(*library);
}

bool all_finite(const toi::plant::PlantArchitecture& architecture)
{
    for (const auto& module : architecture.modules) {
        if (!std::isfinite(module.origin.x) || !std::isfinite(module.origin.y) || !std::isfinite(module.origin.z)) {
            return false;
        }
        for (const auto& segment : module.snapshot.segments) {
            if (!std::isfinite(segment.child_position.x) || !std::isfinite(segment.diameter)) {
                return false;
            }
        }
    }
    return true;
}

} // namespace

TEST_CASE("develop_plant returns a single root module at age zero")
{
    const auto library = load_library();
    const auto plant_type = toi::growth::plant_type_preset_by_key('e');
    REQUIRE(plant_type.has_value());

    const auto architecture = toi::plant::develop_plant(*plant_type, library, 0.0F);
    REQUIRE(architecture.has_value());
    REQUIRE(architecture->modules.size() == 1);
    REQUIRE(architecture->modules.front().parent_module == toi::plant::kNoParent);
}

TEST_CASE("develop_plant is deterministic")
{
    const auto library = load_library();
    const auto plant_type = toi::growth::plant_type_preset_by_key('g');
    REQUIRE(plant_type.has_value());

    const auto first = toi::plant::develop_plant(*plant_type, library, 120.0F);
    const auto second = toi::plant::develop_plant(*plant_type, library, 120.0F);
    REQUIRE(first.has_value());
    REQUIRE(second.has_value());
    REQUIRE(first->modules.size() == second->modules.size());
    REQUIRE(first->modules.back().origin.x == second->modules.back().origin.x);
    REQUIRE(first->modules.back().physiological_age == second->modules.back().physiological_age);
}

TEST_CASE("every built-in species develops to a bounded, finite plant")
{
    const auto library = load_library();
    for (char key = 'a'; key <= 'p'; ++key) {
        const auto plant_type = toi::growth::plant_type_preset_by_key(key);
        REQUIRE(plant_type.has_value());

        const float age = std::min(plant_type->plant_max_age, 300.0F);
        const auto architecture = toi::plant::develop_plant(*plant_type, library, age);
        INFO("preset " << key);
        REQUIRE(architecture.has_value());
        REQUIRE(architecture->modules.size() >= 1);
        // Vigor budget (v̄_rootmax / v̄_min) bounds module count well under this cap.
        REQUIRE(architecture->modules.size() < 500);
        REQUIRE(all_finite(*architecture));

        const auto summary = toi::plant::summarize(*architecture);
        REQUIRE(summary.module_count == architecture->modules.size());
    }
}

TEST_CASE("a plant grows beyond its root over developmental time")
{
    const auto library = load_library();
    const auto plant_type = toi::growth::plant_type_preset_by_key('i');
    REQUIRE(plant_type.has_value());

    const auto young = toi::plant::develop_plant(*plant_type, library, 5.0F);
    const auto old = toi::plant::develop_plant(*plant_type, library, plant_type->plant_max_age);
    REQUIRE(young.has_value());
    REQUIRE(old.has_value());
    INFO("young modules " << young->modules.size() << " old modules " << old->modules.size());
    REQUIRE(old->modules.size() > young->modules.size());
    REQUIRE(old->modules.size() > 1);
}

TEST_CASE("every built-in species attaches modules by end of life")
{
    const auto library = load_library();
    std::size_t multi_module_species = 0;
    for (char key = 'a'; key <= 'p'; ++key) {
        const auto plant_type = toi::growth::plant_type_preset_by_key(key);
        REQUIRE(plant_type.has_value());
        const auto architecture = toi::plant::develop_plant(*plant_type, library, plant_type->plant_max_age);
        REQUIRE(architecture.has_value());
        INFO("preset " << key << " modules " << architecture->modules.size());
        if (architecture->modules.size() > 1) {
            ++multi_module_species;
        }
    }
    // Every built-in species should grow past a single module over its lifespan.
    REQUIRE(multi_module_species == 16);
}
