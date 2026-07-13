#include "toi/growth/growth.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>

namespace {

toi::growth::BranchModulePrototype make_root_prototype(std::size_t id)
{
    using namespace toi::growth;
    BranchModulePrototype prototype;
    prototype.id = id;
    prototype.name = "prototype-" + std::to_string(id);
    prototype.root_node = 0;
    prototype.nodes = {
        {{5.0F, 0.0F, 2.0F}, 0.0F},
        {{5.0F, 0.0F, 3.0F}, 0.0F},
        {{6.0F, 0.0F, 3.0F}, 0.0F},
        {{4.0F, 0.0F, 3.0F}, 0.0F},
    };
    prototype.segments = {
        {0, 1, {0.0F, 0.0F, 1.0F}, std::sqrt(2.0F), 0.0F, 1.0F, 1.0F},
        {1, 2, {1.0F, 0.0F, 0.0F}, 1.0F, 0.0F, 1.0F, 1.0F},
        {1, 3, {-1.0F, 0.0F, 0.0F}, 1.0F, 0.0F, 1.0F, 1.0F},
    };
    prototype.terminal_nodes = {2, 3};
    prototype.child_segments_by_node = {{0}, {1, 2}, {}, {}};
    prototype.incoming_segment_by_node = {std::nullopt, 0, 1, 2};
    return prototype;
}

toi::growth::BranchModulePrototypeLibrary make_library()
{
    toi::growth::BranchModulePrototypeLibrary library;
    for (std::size_t id = 0; id < 9; ++id) library.prototypes.push_back(make_root_prototype(id));
    return library;
}

toi::growth::PlantTypeParameters make_plant_type()
{
    auto plant_type = *toi::growth::plant_type_preset_by_key('a');
    plant_type.tropism_strength = 0.0F;
    return plant_type;
}

const toi::growth::PlantModuleSnapshot& module_by_id(const toi::growth::PlantSnapshot& snapshot, std::size_t id)
{
    return *std::ranges::find(snapshot.modules, id, &toi::growth::PlantModuleSnapshot::id);
}

std::span<const toi::growth::PlantSegmentSnapshot> module_segments(const toi::growth::PlantSnapshot& snapshot,
                                                                   const toi::growth::PlantModuleSnapshot& module)
{
    return snapshot.segments.subspan(module.segments.offset, module.segments.count);
}

} // namespace

TEST_CASE("root-only plant starts at origin with stable flat snapshot storage")
{
    using namespace toi::growth;
    const auto plant_type = make_plant_type();
    auto simulation = PlantSimulation::create(make_library(), plant_type, 7);
    REQUIRE(simulation);
    const auto snapshot = simulation->snapshot();
    const auto repeated = simulation->snapshot();
    REQUIRE(snapshot.modules.size() == 1);
    CHECK(snapshot.modules.data() == repeated.modules.data());
    CHECK(snapshot.segments.data() == repeated.segments.data());
    const auto& root = snapshot.modules.front();
    CHECK(root.id == 0);
    CHECK(root.prototype_id == 7);
    CHECK(root.root_position.x == Catch::Approx(0.0F));
    CHECK(root.root_position.z == Catch::Approx(0.0F));
    CHECK(root.physiological_age == Catch::Approx(0.0F));
    CHECK(root.segments.count == 3);
    CHECK(root.collision_sphere.radius == Catch::Approx(0.0F));
    CHECK(root.direct_light_exposure == Catch::Approx(1.0F));
    CHECK(root.accumulated_light == Catch::Approx(1.0F));
    CHECK(root.vigor == Catch::Approx(kMaximumModuleVigor));
    CHECK(root.growth_rate == Catch::Approx(plant_type.plant_growth_rate));
    CHECK(snapshot.mature_terminals.empty());
    CHECK(snapshot.flow_paths.empty());
    CHECK(snapshot.attachment_events.empty());
}

TEST_CASE("root budget remains separate from module vigor")
{
    using namespace toi::growth;
    auto plant_type = make_plant_type();
    plant_type.root_max_vigor = 0.5F;
    auto simulation = PlantSimulation::create(make_library(), plant_type, 7);
    REQUIRE(simulation);
    const auto& root = simulation->snapshot().modules.front();
    const float normalized = (0.5F - kMinimumModuleVigor) / (kMaximumModuleVigor - kMinimumModuleVigor);
    const float expected_rate = (3.0F * normalized * normalized - 2.0F * normalized * normalized * normalized) *
                                plant_type.plant_growth_rate;
    CHECK(root.vigor == Catch::Approx(0.5F));
    CHECK(root.growth_rate == Catch::Approx(expected_rate));
}

TEST_CASE("maturity crossing atomically attaches every eligible root terminal")
{
    using namespace toi::growth;
    auto simulation = PlantSimulation::create(make_library(), make_plant_type(), 7);
    REQUIRE(simulation);
    REQUIRE(simulation->step(10'000.0F));
    auto attached = simulation->snapshot();
    REQUIRE(attached.modules.size() == 3);
    REQUIRE(attached.attachment_events.size() == 2);
    CHECK(attached.attachment_events[0].child_module_id == 1);
    CHECK(attached.attachment_events[1].child_module_id == 2);
    CHECK(attached.attachment_events[0].parent_terminal_node == 2);
    CHECK(attached.attachment_events[1].parent_terminal_node == 3);
    CHECK(attached.attachment_events[0].prototype_id == attached.attachment_events[1].prototype_id);
    for (std::size_t id = 1; id <= 2; ++id) {
        const auto& child = module_by_id(attached, id);
        CHECK(child.parent_module_id == 0);
        CHECK(child.physiological_age == Catch::Approx(0.0F));
        CHECK(child.direct_light_exposure == Catch::Approx(0.0F));
        CHECK(child.vigor == Catch::Approx(0.0F));
    }
    REQUIRE(attached.mature_terminals.size() == 2);
    CHECK(attached.mature_terminals[0].child_module_id == 1);
    CHECK(attached.mature_terminals[1].child_module_id == 2);
    CHECK(module_by_id(attached, 1).root_position.x ==
          Catch::Approx(attached.mature_terminals[0].position.x));
    CHECK(module_by_id(attached, 1).root_position.z ==
          Catch::Approx(attached.mature_terminals[0].position.z));

    const auto child_transform = module_by_id(attached, 1).transform;
    REQUIRE(simulation->step(1.0F));
    const auto activated = simulation->snapshot();
    CHECK(activated.modules.size() == 3);
    CHECK(activated.attachment_events.empty());
    CHECK(module_by_id(activated, 1).physiological_age > 0.0F);
    CHECK(module_by_id(activated, 1).direct_light_exposure > 0.0F);
    CHECK(module_by_id(activated, 1).transform.z_axis.x == Catch::Approx(child_transform.z_axis.x));
    CHECK_FALSE(activated.flow_paths.empty());
}

TEST_CASE("continuous pipe crosses parent and child module attachment")
{
    using namespace toi::growth;
    auto simulation = PlantSimulation::create(make_library(), make_plant_type(), 7);
    REQUIRE(simulation);
    REQUIRE(simulation->step(10'000.0F));
    const auto snapshot = simulation->snapshot();
    const auto& root = module_by_id(snapshot, 0);
    const auto& child = module_by_id(snapshot, 1);
    const auto root_segments = module_segments(snapshot, root);
    const auto child_segments = module_segments(snapshot, child);
    CHECK(root_segments[1].target_diameter == Catch::Approx(child_segments[0].target_diameter));
    CHECK(root_segments[0].target_diameter > child_segments[0].target_diameter);
    CHECK(root_segments[0].main_continuation_segment == root.segments.offset + 1);
}

TEST_CASE("root vigor budget can exceed one module's vigor")
{
    using namespace toi::growth;
    auto plant_type = make_plant_type();
    plant_type.root_max_vigor = 2.0F;
    auto simulation = PlantSimulation::create(make_library(), plant_type, 7);
    REQUIRE(simulation);
    REQUIRE(simulation->step(10'000.0F));
    REQUIRE(simulation->step(1.0F));
    const auto snapshot = simulation->snapshot();
    CHECK(module_by_id(snapshot, 0).vigor == Catch::Approx(kMaximumModuleVigor));
    CHECK(module_by_id(snapshot, 1).vigor == Catch::Approx(kMaximumModuleVigor));
    const auto vigor_path = std::ranges::find(snapshot.flow_paths, FlowKind::Vigor, &PlantFlowPath::kind);
    REQUIRE(vigor_path != snapshot.flow_paths.end());
    CHECK(vigor_path->root_total == Catch::Approx(2.0F));
}

TEST_CASE("accumulated-light and vigor flow remain deterministic without grandchildren")
{
    using namespace toi::growth;
    auto first = PlantSimulation::create(make_library(), make_plant_type(), 7);
    auto second = PlantSimulation::create(make_library(), make_plant_type(), 7);
    REQUIRE(first);
    REQUIRE(second);
    for (const float timestep : {10'000.0F, 100.0F, 10'000.0F}) {
        REQUIRE(first->step(timestep));
        REQUIRE(second->step(timestep));
    }
    const auto left = first->snapshot();
    const auto right = second->snapshot();
    REQUIRE(left.modules.size() == 3);
    REQUIRE(left.modules.size() == right.modules.size());
    CHECK(left.flow_paths.size() == right.flow_paths.size());
    CHECK(module_by_id(left, 1).physiological_age == Catch::Approx(module_by_id(right, 1).physiological_age));
    CHECK(left.mature_terminals.size() >= 6);
    for (const auto& terminal : left.mature_terminals) {
        if (terminal.module_id != 0) CHECK_FALSE(terminal.child_module_id);
    }
}

TEST_CASE("plant simulation rejects invalid inputs without mutation")
{
    using namespace toi::growth;
    auto incomplete_library = make_library();
    incomplete_library.prototypes.pop_back();
    CHECK_FALSE(PlantSimulation::create(incomplete_library, make_plant_type(), 7));
    CHECK_FALSE(PlantSimulation::create(make_library(), make_plant_type(), 99));

    auto simulation = PlantSimulation::create(make_library(), make_plant_type(), 7);
    REQUIRE(simulation);
    const auto before = simulation->snapshot();
    CHECK_FALSE(simulation->step(0.0F));
    CHECK_FALSE(simulation->step(-1.0F));
    const auto after = simulation->snapshot();
    CHECK(after.plant_age == Catch::Approx(before.plant_age));
    CHECK(after.modules.data() == before.modules.data());
}
