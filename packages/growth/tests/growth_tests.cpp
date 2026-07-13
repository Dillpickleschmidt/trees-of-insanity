#include "toi/growth/growth.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
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
        {{0.0F, 0.0F, 0.30F}, 0.0F},
        {{-0.12F, 0.0F, 0.50F}, 0.0F},
        {{0.12F, 0.0F, 0.50F}, 0.0F},
    };
    prototype.segments = {
        {0, 1, {0.0F, 0.0F, 1.0F}, std::sqrt(2.0F), 0.0F, 1.0F, 0.30F},
        {1, 2, {-0.5145F, 0.0F, 0.8575F}, 1.0F, 0.0F, 1.0F, 0.233238F},
        {1, 3, {0.5145F, 0.0F, 0.8575F}, 1.0F, 0.0F, 1.0F, 0.233238F},
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

TEST_CASE("Table structural parameters produce meter-based module geometry")
{
    using namespace toi::growth;
    const auto plant_type = plant_type_preset_by_key('a');
    REQUIRE(plant_type);
    CHECK(plant_type->terminal_thickness == Catch::Approx(0.57F));
    CHECK(plant_type->length_growth_scale == Catch::Approx(0.47F));

    const auto prepared = prepare_branch_module_prototype(make_prototype(0), *plant_type);
    REQUIRE(prepared);
    const auto mature_age = fully_grown_age(*prepared, *plant_type);
    REQUIRE(mature_age);
    CHECK(*mature_age == Catch::Approx(113.4549F));

    const auto snapshot = make_growth_snapshot(*prepared, *plant_type, *mature_age);
    REQUIRE(snapshot);
    REQUIRE(snapshot->segments.size() == 3);
    const auto root = std::ranges::find(snapshot->segments, 0, &GrowthSnapshotSegment::source_segment_id);
    const auto first_terminal = std::ranges::find(snapshot->segments, 1, &GrowthSnapshotSegment::source_segment_id);
    REQUIRE(root != snapshot->segments.end());
    REQUIRE(first_terminal != snapshot->segments.end());
    CHECK(distance(root->parent_position, root->child_position) == Catch::Approx(0.30F));
    CHECK(distance(first_terminal->parent_position, first_terminal->child_position) == Catch::Approx(0.233238F));
    CHECK(root->diameter == Catch::Approx(std::sqrt(2.0F) * 0.0057F));
    CHECK(first_terminal->diameter == Catch::Approx(0.0057F));
}

TEST_CASE("prototype preparation precomputes main-axis continuation")
{
    using namespace toi::growth;
    auto prototype = make_prototype(0);
    prototype.segments[1].direction = {0.0F, 0.0F, 1.0F};
    prototype.segments[2].direction = {0.0871557F, 0.0F, 0.996195F};
    prototype.segments[2].pipe_diameter_factor = 2.0F;
    const auto plant_type = plant_type_preset_by_key('a');
    REQUIRE(plant_type);
    auto prepared = prepare_branch_module_prototype(prototype, *plant_type);
    REQUIRE(prepared);
    REQUIRE(prepared->main_child_segment_by_node[1]);
    CHECK(*prepared->main_child_segment_by_node[1] == 2);

    prototype.segments[2].pipe_diameter_factor = 1.0F;
    prepared = prepare_branch_module_prototype(prototype, *plant_type);
    REQUIRE(prepared);
    CHECK(*prepared->main_child_segment_by_node[1] == 1);

    prototype.segments[2].direction = {0.342020F, 0.0F, 0.939693F};
    prototype.segments[2].pipe_diameter_factor = 10.0F;
    prepared = prepare_branch_module_prototype(prototype, *plant_type);
    REQUIRE(prepared);
    CHECK(*prepared->main_child_segment_by_node[1] == 1);

    prototype.segments[2].direction = prototype.segments[1].direction;
    prototype.segments[2].pipe_diameter_factor = prototype.segments[1].pipe_diameter_factor;
    prototype.segments[2].max_length = prototype.segments[1].max_length * 2.0F;
    prepared = prepare_branch_module_prototype(prototype, *plant_type);
    REQUIRE(prepared);
    CHECK(*prepared->main_child_segment_by_node[1] == 2);

    prototype.segments[2].max_length = prototype.segments[1].max_length;
    prepared = prepare_branch_module_prototype(prototype, *plant_type);
    REQUIRE(prepared);
    CHECK(*prepared->main_child_segment_by_node[1] == 1);
    CHECK_FALSE(prepared->main_child_segment_by_node[prototype.root_node]);

    prototype.segments[2].direction = {};
    CHECK_FALSE(prepare_branch_module_prototype(prototype, *plant_type));
}

TEST_CASE("three-sphere collision exposure changes gradually with raw cubic-meter volume")
{
    using namespace toi::growth;
    const Sphere own{{0.0F, 0.0F, 0.0F}, 1.0F};
    const std::array separated{
        Sphere{{2.0F, 0.0F, 0.0F}, 1.0F},
        Sphere{{-2.0F, 0.0F, 0.0F}, 1.0F},
    };
    const std::array partial{
        Sphere{{1.0F, 0.0F, 0.0F}, 1.0F},
        Sphere{{-1.0F, 0.0F, 0.0F}, 1.0F},
    };
    const std::array greater_overlap{
        Sphere{{0.0F, 0.0F, 0.0F}, 1.0F},
        Sphere{{0.0F, 0.0F, 0.0F}, 1.0F},
    };
    const std::array first_partial_only{partial[0]};
    const std::array second_partial_only{partial[1]};

    const float separated_volume = collision_measure(own, separated);
    const float partial_volume = collision_measure(own, partial);
    const float greater_volume = collision_measure(own, greater_overlap);
    CHECK(separated_volume == Catch::Approx(0.0F));
    CHECK(partial_volume == Catch::Approx(5.0F * std::numbers::pi_v<float> / 6.0F));
    CHECK(partial_volume == Catch::Approx(collision_measure(own, first_partial_only) +
                                          collision_measure(own, second_partial_only)));
    CHECK(greater_volume == Catch::Approx(8.0F * std::numbers::pi_v<float> / 3.0F));
    CHECK(separated_volume < partial_volume);
    CHECK(partial_volume < greater_volume);
    CHECK(light_exposure(separated_volume) > light_exposure(partial_volume));
    CHECK(light_exposure(partial_volume) > light_exposure(greater_volume));
}

TEST_CASE("Borchert-Honda split conserves vigor")
{
    const auto split = toi::growth::split_vigor(80.0F, 3.0F, 1.0F, 0.75F);
    CHECK(split.main_axis == Catch::Approx(72.0F));
    CHECK(split.lateral_axis == Catch::Approx(8.0F));
    CHECK(split.main_axis + split.lateral_axis == Catch::Approx(80.0F));
    const auto zero_light = toi::growth::split_vigor(80.0F, 0.0F, 0.0F, 0.75F);
    CHECK(zero_light.main_axis == Catch::Approx(60.0F));
    CHECK(zero_light.lateral_axis == Catch::Approx(20.0F));
}

TEST_CASE("full-vigor determinacy selects the nearest fixed morphospace cell")
{
    using namespace toi::growth;
    CHECK(vigor_scaled_determinacy(0.8F, 1.0F) == Catch::Approx(0.8F));
    CHECK(nearest_morphospace_prototype(0.5F, vigor_scaled_determinacy(0.8F, 1.0F)) == 5);
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
