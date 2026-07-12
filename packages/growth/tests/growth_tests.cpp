#include "toi/growth/growth.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <numbers>

namespace {

toi::growth::BranchModulePrototype make_prototype(std::size_t id)
{
    using namespace toi::growth;
    BranchModulePrototype prototype;
    prototype.id = id;
    prototype.name = "prototype";
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

TEST_CASE("sphere collision measure is normalized by the subject sphere volume")
{
    using namespace toi::growth;
    const Sphere own{{0.0F, 0.0F, 0.0F}, 2.0F};
    const std::array others{Sphere{{0.0F, 0.0F, 0.0F}, 2.0F}};
    CHECK(normalized_collision_measure(own, others) == Catch::Approx(1.0F));
    CHECK(collision_measure(own, others) == Catch::Approx(32.0F * std::numbers::pi_v<float> / 3.0F));
    CHECK(light_exposure(1.0F) == Catch::Approx(std::exp(-1.0F)));
}

TEST_CASE("Borchert-Honda split conserves vigor")
{
    const auto split = toi::growth::split_vigor(80.0F, 3.0F, 1.0F, 0.75F);
    CHECK(split.main_axis == Catch::Approx(72.0F));
    CHECK(split.lateral_axis == Catch::Approx(8.0F));
    CHECK(split.main_axis + split.lateral_axis == Catch::Approx(80.0F));
}

TEST_CASE("full-vigor determinacy selects the nearest fixed morphospace cell")
{
    using namespace toi::growth;
    CHECK(vigor_scaled_determinacy(0.8F, 100.0F, 100.0F) == Catch::Approx(0.8F));
    CHECK(nearest_morphospace_prototype(0.5F, vigor_scaled_determinacy(0.8F, 100.0F, 100.0F)) == 5);
}

TEST_CASE("orientation costs implement equations 3 and 4")
{
    using namespace toi::growth;
    const float tropism = orientation_tropism_cost(0.0F, {1.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 1.0F});
    CHECK(tropism == Catch::Approx(1.0F));
    CHECK(orientation_distribution_cost(0.25F, 2.0F, tropism, 3.0F) == Catch::Approx(3.5F));
}

TEST_CASE("age integration shedding and senescence policies are explicit")
{
    using namespace toi::growth;
    CHECK(physiological_age_euler_step(2.0F, 0.5F, 4.0F) == Catch::Approx(4.0F));
    CHECK(shedding_vigor_threshold(500.0F) == Catch::Approx(10.0F));
    CHECK(senescence_interpolation(100.0F, 25.0F, 20.0F, 10.0F) == Catch::Approx(50.0F));
    CHECK(parameter_for_plant_age(0.9F, 0.4F, 20.0F, 20.0F) == Catch::Approx(0.4F));
}
