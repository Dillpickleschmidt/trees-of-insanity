#include "toi/growth/growth.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

namespace {

toi::growth::BranchModulePrototype make_root_prototype(std::size_t id)
{
    using namespace toi::growth;
    BranchModulePrototype prototype;
    prototype.id = id;
    prototype.name = "root";
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
    return {.prototypes = {make_root_prototype(7)}};
}

toi::growth::PlantTypeParameters make_plant_type()
{
    auto plant_type = *toi::growth::plant_type_preset_by_key('a');
    plant_type.tropism_strength = 0.0F;
    return plant_type;
}

} // namespace

TEST_CASE("root-only plant starts at the world origin with committed diagnostics")
{
    using namespace toi::growth;
    const auto plant_type = make_plant_type();
    auto simulation = PlantSimulation::create(make_library(), plant_type, 7);
    REQUIRE(simulation);

    const auto snapshot = simulation->snapshot();
    CHECK(snapshot.plant_age == Catch::Approx(0.0F));
    REQUIRE(snapshot.modules.size() == 1);
    const auto& root = snapshot.modules.front();
    CHECK(root.id == 0);
    CHECK(root.prototype_id == 7);
    CHECK(root.root_position.x == Catch::Approx(0.0F));
    CHECK(root.root_position.y == Catch::Approx(0.0F));
    CHECK(root.root_position.z == Catch::Approx(0.0F));
    CHECK(root.physiological_age == Catch::Approx(0.0F));
    CHECK(root.segments.empty());
    CHECK(root.collision_sphere.center.x == Catch::Approx(0.0F));
    CHECK(root.collision_sphere.center.y == Catch::Approx(0.0F));
    CHECK(root.collision_sphere.center.z == Catch::Approx(0.0F));
    CHECK(root.collision_sphere.radius == Catch::Approx(0.0F));
    CHECK(root.direct_light_exposure == Catch::Approx(1.0F));
    CHECK(root.accumulated_light == Catch::Approx(1.0F));
    CHECK(root.vigor == Catch::Approx(kMaximumModuleVigor));
    CHECK(root.growth_rate == Catch::Approx(plant_type.plant_growth_rate));
}

TEST_CASE("root budget and module vigor remain separate")
{
    using namespace toi::growth;
    auto plant_type = make_plant_type();
    plant_type.root_max_vigor = 0.5F;
    auto simulation = PlantSimulation::create(make_library(), plant_type, 7);
    REQUIRE(simulation);

    const auto& root = simulation->snapshot().modules.front();
    const float normalized_vigor = (0.5F - kMinimumModuleVigor) / (kMaximumModuleVigor - kMinimumModuleVigor);
    const float expected_rate =
        (3.0F * normalized_vigor * normalized_vigor - 2.0F * normalized_vigor * normalized_vigor * normalized_vigor) *
        plant_type.plant_growth_rate;
    CHECK(root.vigor == Catch::Approx(0.5F));
    CHECK(root.growth_rate == Catch::Approx(expected_rate));
}

TEST_CASE("plant step atomically advances calendar and physiological age")
{
    using namespace toi::growth;
    auto simulation = PlantSimulation::create(make_library(), make_plant_type(), 7);
    REQUIRE(simulation);
    const float initial_rate = simulation->snapshot().modules.front().growth_rate;

    REQUIRE(simulation->step(2.0F));
    const auto snapshot = simulation->snapshot();
    REQUIRE(snapshot.modules.size() == 1);
    CHECK(snapshot.plant_age == Catch::Approx(2.0F));
    CHECK(snapshot.modules.front().physiological_age == Catch::Approx(initial_rate * 2.0F));
    CHECK(snapshot.modules.front().collision_sphere.radius > 0.0F);
    CHECK(snapshot.modules.front().direct_light_exposure == Catch::Approx(1.0F));
    CHECK(snapshot.modules.front().accumulated_light == Catch::Approx(1.0F));
}

TEST_CASE("plant collision sphere uses unique developed topology nodes")
{
    using namespace toi::growth;
    auto simulation = PlantSimulation::create(make_library(), make_plant_type(), 7);
    REQUIRE(simulation);
    REQUIRE(simulation->step(10'000.0F));

    const auto& root = simulation->snapshot().modules.front();
    REQUIRE(root.segments.size() == 3);
    CHECK(root.collision_sphere.center.x == Catch::Approx(0.0F));
    CHECK(root.collision_sphere.center.y == Catch::Approx(0.0F));
    CHECK(root.collision_sphere.center.z == Catch::Approx(0.75F));
    CHECK(root.collision_sphere.radius == Catch::Approx(std::sqrt(1.0625F)));
}

TEST_CASE("root-only plant snapshots are stable between mutations and never attach")
{
    using namespace toi::growth;
    auto first = PlantSimulation::create(make_library(), make_plant_type(), 7);
    auto second = PlantSimulation::create(make_library(), make_plant_type(), 7);
    REQUIRE(first);
    REQUIRE(second);
    REQUIRE(first->step(4.0F));
    REQUIRE(second->step(4.0F));

    const auto before = first->snapshot();
    const auto repeated = first->snapshot();
    CHECK(before.modules.data() == repeated.modules.data());
    CHECK(before.modules.front().segments.data() == repeated.modules.front().segments.data());
    CHECK(before.modules.front().physiological_age ==
          Catch::Approx(second->snapshot().modules.front().physiological_age));

    REQUIRE(first->step(10'000.0F));
    const float mature_age = first->snapshot().modules.front().physiological_age;
    const auto mature_segments = first->snapshot().modules.front().segments.size();
    REQUIRE(first->step(10.0F));
    const auto mature = first->snapshot();
    CHECK(mature.modules.size() == 1);
    CHECK(mature.modules.front().physiological_age == Catch::Approx(mature_age));
    CHECK(mature.modules.front().segments.size() == mature_segments);
    CHECK(mature.plant_age == Catch::Approx(10'014.0F));
}

TEST_CASE("plant simulation rejects invalid creation and step inputs")
{
    using namespace toi::growth;
    auto invalid_type = make_plant_type();
    invalid_type.length_growth_scale = 0.0F;
    CHECK_FALSE(PlantSimulation::create(make_library(), invalid_type, 7));
    CHECK_FALSE(PlantSimulation::create(make_library(), make_plant_type(), 99));

    auto simulation = PlantSimulation::create(make_library(), make_plant_type(), 7);
    REQUIRE(simulation);
    CHECK_FALSE(simulation->step(0.0F));
    CHECK_FALSE(simulation->step(-1.0F));
}
