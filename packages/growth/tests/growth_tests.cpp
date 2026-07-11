#include "toi/growth/growth.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cmath>

namespace {

toi::growth::BranchModulePrototype make_prototype(std::size_t id)
{
    using namespace toi::growth;
    BranchModulePrototype prototype;
    prototype.id = id;
    prototype.name = id == 0 ? "Cube" : "Cube." + std::string(id < 10 ? "00" : "0") + std::to_string(id);
    prototype.root_node = 0;
    prototype.nodes = {
        {{0.0F, 0.0F, 0.0F}, 0.0F},
        {{0.0F, 0.0F, 30.0F}, 0.0F},
        {{-12.0F, 0.0F, 50.0F}, 0.0F},
        {{12.0F, 0.0F, 50.0F}, 0.0F},
    };
    prototype.segments = {
        {0, 1, {0.0F, 0.0F, 1.0F}, 0.0F, 0.0F, 1.0F, 30.0F},
        {1, 2, {-0.5145F, 0.0F, 0.8575F}, 0.0F, 0.0F, 1.0F, 23.3238F},
        {1, 3, {0.5145F, 0.0F, 0.8575F}, 0.0F, 0.0F, 1.0F, 23.3238F},
    };
    prototype.terminal_nodes = {2, 3};
    prototype.child_segments_by_node = {{0}, {1, 2}, {}, {}};
    prototype.incoming_segment_by_node = {std::nullopt, 0, 1, 2};
    return prototype;
}

toi::growth::BranchModulePrototypeLibrary make_library()
{
    toi::growth::BranchModulePrototypeLibrary library;
    for (std::size_t id = 0; id < 9; ++id) {
        library.prototypes.push_back(make_prototype(id));
    }
    return library;
}

bool finite(toi::growth::Vec3 value)
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

} // namespace

TEST_CASE("module growth is deterministic")
{
    using namespace toi::growth;
    const auto plant_type = plant_type_preset_by_key('a');
    REQUIRE(plant_type);
    const auto prepared = prepare_branch_module_prototype(make_prototype(0), *plant_type);
    REQUIRE(prepared);

    const auto first = make_growth_snapshot(*prepared, *plant_type, 20.0F);
    const auto second = make_growth_snapshot(*prepared, *plant_type, 20.0F);
    REQUIRE(first);
    REQUIRE(second);
    REQUIRE(first->segments.size() == second->segments.size());
    REQUIRE(first->module_physiological_age == second->module_physiological_age);
}

TEST_CASE("every preset develops a bounded finite plant")
{
    using namespace toi::growth;
    const auto library = make_library();
    for (char key = 'a'; key <= 'p'; ++key) {
        const auto plant_type = plant_type_preset_by_key(key);
        REQUIRE(plant_type);
        const auto architecture = develop_plant(*plant_type, library, plant_type->plant_max_age);
        REQUIRE(architecture);
        REQUIRE_FALSE(architecture->modules.empty());
        REQUIRE(architecture->modules.size() <= 128);
        for (const auto& module : architecture->modules) {
            REQUIRE(finite(module.origin));
            REQUIRE(std::isfinite(module.physiological_age));
            REQUIRE(std::isfinite(module.vigor));
        }
    }
}

TEST_CASE("plant development is deterministic")
{
    using namespace toi::growth;
    const auto plant_type = plant_type_preset_by_key('h');
    REQUIRE(plant_type);
    const auto library = make_library();
    const auto first = develop_plant(*plant_type, library, 120.0F);
    const auto second = develop_plant(*plant_type, library, 120.0F);
    REQUIRE(first);
    REQUIRE(second);
    REQUIRE(first->modules.size() == second->modules.size());
    REQUIRE(summarize(*first).visible_segment_count == summarize(*second).visible_segment_count);
}
