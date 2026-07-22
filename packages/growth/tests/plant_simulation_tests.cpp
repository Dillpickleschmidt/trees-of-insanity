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
        {0, 1, {0.0F, 0.0F, 1.0F}, std::sqrt(2.0F), 1.0F, 1.0F, 1.0F},
        {1, 2, {1.0F, 0.0F, 0.0F}, 1.0F, 1.0F, 1.0F, 1.0F},
        {1, 3, {-1.0F, 0.0F, 0.0F}, 1.0F, 1.0F, 1.0F, 1.0F},
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

toi::growth::BranchModulePrototypeLibrary make_colliding_sibling_library()
{
    auto library = make_library();
    auto& root = library.prototypes[7];
    root.nodes[3].position = root.nodes[2].position;
    root.segments[2].direction = root.segments[1].direction;
    return library;
}

toi::growth::BranchModulePrototypeLibrary make_unordered_terminal_library()
{
    auto library = make_library();
    auto& root = library.prototypes[7];
    root.nodes.push_back({{5.5F, 0.8660254F, 3.0F}, 0.0F});
    root.segments.push_back({1, 4, {0.5F, 0.8660254F, 0.0F}, 1.0F, 1.0F, 1.0F, 1.0F});
    root.terminal_nodes = {3, 4, 2};
    root.child_segments_by_node[1].push_back(3);
    root.child_segments_by_node.push_back({});
    root.incoming_segment_by_node.push_back(3);
    return library;
}

toi::growth::BranchModulePrototype make_linear_prototype(std::size_t id)
{
    using namespace toi::growth;
    return {
        .id = id,
        .name = "linear-" + std::to_string(id),
        .nodes = {{{0.0F, 0.0F, 0.0F}, 0.0F}, {{0.0F, 0.0F, 1.0F}, 0.0F}},
        .segments = {{0, 1, {0.0F, 0.0F, 1.0F}, 1.0F, 1.0F, 1.0F, 1.0F}},
        .root_node = 0,
        .terminal_nodes = {1},
        .child_segments_by_node = {{0}, {}},
        .incoming_segment_by_node = {std::nullopt, 0},
    };
}

toi::growth::BranchModulePrototypeLibrary make_repeated_attachment_library()
{
    using namespace toi::growth;
    BranchModulePrototypeLibrary library;
    for (std::size_t id = 0; id < 9; ++id) {
        library.prototypes.push_back(make_linear_prototype(id));
    }
    library.prototypes[7] = {
        .id = 7,
        .name = "symmetric-root",
        .nodes = {
            {{0.0F, 0.0F, 0.0F}, 0.0F},
            {{0.0F, 0.0F, 1.0F}, 0.0F},
            {{-2.5F, 0.0F, 1.0F}, 0.0F},
            {{-1.5F, 0.0F, 1.0F}, 0.0F},
            {{2.5F, 0.0F, 1.0F}, 0.0F},
            {{1.5F, 0.0F, 1.0F}, 0.0F},
        },
        .segments = {
            {0, 1, {0.0F, 0.0F, 1.0F}, 1.0F, 1.0F, 1.0F, 1.0F},
            {1, 2, {-1.0F, 0.0F, 0.0F}, 1.0F, 1.0F, 1.0F, 2.5F},
            {2, 3, {1.0F, 0.0F, 0.0F}, 1.0F, 1.0F, 1.0F, 1.0F},
            {1, 4, {1.0F, 0.0F, 0.0F}, 1.0F, 1.0F, 1.0F, 2.5F},
            {4, 5, {-1.0F, 0.0F, 0.0F}, 1.0F, 1.0F, 1.0F, 1.0F},
        },
        .root_node = 0,
        .terminal_nodes = {5, 3},
        .child_segments_by_node = {{0}, {1, 3}, {2}, {}, {4}, {}},
        .incoming_segment_by_node = {std::nullopt, 0, 1, 2, 3, 4},
    };
    return library;
}

toi::growth::PlantTypeParameters make_plant_type()
{
    auto plant_type = *toi::growth::plant_type_preset_by_key('a');
    plant_type.tropism_strength = 0.0F;
    return plant_type;
}

toi::growth::PlantTypeParameters make_repeated_attachment_plant_type()
{
    auto plant_type = make_plant_type();
    plant_type.root_max_vigor = 1.0F;
    plant_type.apical_control = 0.8F;
    plant_type.determinacy = 1.0F;
    plant_type.mature_apical_control.reset();
    plant_type.mature_determinacy.reset();
    plant_type.flowering_age = 0.0F;
    plant_type.tropism_weight = 0.0F;
    return plant_type;
}

toi::growth::PlantTypeParameters make_shedding_plant_type()
{
    auto plant_type = make_repeated_attachment_plant_type();
    plant_type.apical_control = 0.5F;
    plant_type.mature_apical_control = 0.99F;
    plant_type.flowering_age = 150'000.0F;
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

const toi::growth::MatureTerminalSnapshot& only_terminal(const toi::growth::PlantSnapshot& snapshot,
                                                         std::size_t module_id)
{
    const auto terminal = std::ranges::find(snapshot.mature_terminals, module_id,
                                            &toi::growth::MatureTerminalSnapshot::module_id);
    return *terminal;
}

float direction_alignment(toi::growth::Vec3 left, toi::growth::Vec3 right)
{
    return left.x * right.x + left.y * right.y + left.z * right.z;
}

void check_vec3_equal(toi::growth::Vec3 left, toi::growth::Vec3 right)
{
    CHECK(left.x == right.x);
    CHECK(left.y == right.y);
    CHECK(left.z == right.z);
}

void check_transform_equal(const toi::growth::RigidTransform& left,
                           const toi::growth::RigidTransform& right)
{
    check_vec3_equal(left.x_axis, right.x_axis);
    check_vec3_equal(left.y_axis, right.y_axis);
    check_vec3_equal(left.z_axis, right.z_axis);
    check_vec3_equal(left.translation, right.translation);
}

void check_snapshots_equal(const toi::growth::PlantSnapshot& left,
                           const toi::growth::PlantSnapshot& right)
{
    CHECK(left.plant_age == right.plant_age);
    REQUIRE(left.modules.size() == right.modules.size());
    REQUIRE(left.segments.size() == right.segments.size());
    REQUIRE(left.mature_terminals.size() == right.mature_terminals.size());
    REQUIRE(left.attachment_events.size() == right.attachment_events.size());
    REQUIRE(left.shedding_events.size() == right.shedding_events.size());

    for (std::size_t index = 0; index < left.modules.size(); ++index) {
        const auto& left_module = left.modules[index];
        const auto& right_module = right.modules[index];
        CHECK(left_module.id == right_module.id);
        CHECK(left_module.prototype_id == right_module.prototype_id);
        CHECK(left_module.parent_module_id == right_module.parent_module_id);
        CHECK(left_module.parent_terminal_node == right_module.parent_terminal_node);
        check_transform_equal(left_module.transform, right_module.transform);
        check_vec3_equal(left_module.root_position, right_module.root_position);
        CHECK(left_module.physiological_age == right_module.physiological_age);
        CHECK(left_module.fully_grown_age == right_module.fully_grown_age);
        CHECK(left_module.direct_light_exposure == right_module.direct_light_exposure);
        CHECK(left_module.accumulated_light == right_module.accumulated_light);
        CHECK(left_module.vigor == right_module.vigor);
        CHECK(left_module.growth_rate == right_module.growth_rate);
        check_vec3_equal(left_module.collision_sphere.center, right_module.collision_sphere.center);
        CHECK(left_module.collision_sphere.radius == right_module.collision_sphere.radius);
        CHECK(left_module.segments.offset == right_module.segments.offset);
        CHECK(left_module.segments.count == right_module.segments.count);
    }
    for (std::size_t index = 0; index < left.segments.size(); ++index) {
        const auto& left_segment = left.segments[index];
        const auto& right_segment = right.segments[index];
        CHECK(left_segment.module_id == right_segment.module_id);
        CHECK(left_segment.source_segment_id == right_segment.source_segment_id);
        check_vec3_equal(left_segment.parent_position, right_segment.parent_position);
        check_vec3_equal(left_segment.child_position, right_segment.child_position);
        check_vec3_equal(left_segment.mature_parent_position, right_segment.mature_parent_position);
        check_vec3_equal(left_segment.mature_child_position, right_segment.mature_child_position);
        CHECK(left_segment.diameter == right_segment.diameter);
        CHECK(left_segment.state == right_segment.state);
        CHECK(left_segment.main_continuation_segment == right_segment.main_continuation_segment);
    }
    for (std::size_t index = 0; index < left.mature_terminals.size(); ++index) {
        const auto& left_terminal = left.mature_terminals[index];
        const auto& right_terminal = right.mature_terminals[index];
        CHECK(left_terminal.module_id == right_terminal.module_id);
        CHECK(left_terminal.terminal_node == right_terminal.terminal_node);
        check_vec3_equal(left_terminal.position, right_terminal.position);
        check_vec3_equal(left_terminal.tangent, right_terminal.tangent);
        CHECK(left_terminal.host_radius == right_terminal.host_radius);
        CHECK(left_terminal.vigor == right_terminal.vigor);
        CHECK(left_terminal.axis_role == right_terminal.axis_role);
        CHECK(left_terminal.child_module_id == right_terminal.child_module_id);
    }
    for (std::size_t index = 0; index < left.attachment_events.size(); ++index) {
        const auto& left_event = left.attachment_events[index];
        const auto& right_event = right.attachment_events[index];
        CHECK(left_event.child_module_id == right_event.child_module_id);
        CHECK(left_event.parent_module_id == right_event.parent_module_id);
        CHECK(left_event.parent_terminal_node == right_event.parent_terminal_node);
        CHECK(left_event.prototype_id == right_event.prototype_id);
    }
    for (std::size_t index = 0; index < left.shedding_events.size(); ++index) {
        const auto& left_event = left.shedding_events[index];
        const auto& right_event = right.shedding_events[index];
        CHECK(left_event.module_id == right_event.module_id);
        CHECK(left_event.parent_module_id == right_event.parent_module_id);
        CHECK(left_event.parent_terminal_node == right_event.parent_terminal_node);
    }
}

void check_recursive_light_and_vigor_conservation(const toi::growth::PlantSnapshot& snapshot)
{
    for (std::size_t module_index = 0; module_index < snapshot.modules.size(); ++module_index) {
        const auto& parent = snapshot.modules[module_index];
        float accumulated_light = parent.direct_light_exposure;
        float distributed_vigor = 0.0F;
        std::size_t child_count = 0;
        for (const auto& child : snapshot.modules) {
            if (child.parent_module_id == parent.id) {
                CHECK(child.id > parent.id);
                accumulated_light += child.accumulated_light;
                distributed_vigor += child.vigor;
                ++child_count;
            }
        }
        CHECK(parent.accumulated_light == Catch::Approx(accumulated_light));
        if (child_count > 0) {
            CHECK(parent.vigor == Catch::Approx(distributed_vigor));
        }
    }
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

TEST_CASE("strict shedding threshold can leave an empty dead plant")
{
    using namespace toi::growth;
    auto equality_type = make_plant_type();
    equality_type.root_max_vigor = kMinimumModuleVigor;
    auto equality = PlantSimulation::create(make_library(), equality_type, 7);
    REQUIRE(equality);
    REQUIRE(equality->step(1.0F));
    const auto equality_snapshot = equality->snapshot();
    REQUIRE(equality_snapshot.modules.size() == 1);
    CHECK(equality_snapshot.modules.front().vigor == Catch::Approx(kMinimumModuleVigor));
    CHECK(equality_snapshot.modules.front().growth_rate == Catch::Approx(0.0F));
    CHECK(equality_snapshot.shedding_events.empty());

    auto below_type = make_plant_type();
    below_type.root_max_vigor = kMinimumModuleVigor * 0.5F;
    auto below = PlantSimulation::create(make_library(), below_type, 7);
    REQUIRE(below);
    REQUIRE(below->step(1.0F));
    const auto shed = below->snapshot();
    CHECK(shed.modules.empty());
    CHECK(shed.segments.empty());
    CHECK(shed.mature_terminals.empty());
    CHECK(shed.attachment_events.empty());
    REQUIRE(shed.shedding_events.size() == 1);
    CHECK(shed.shedding_events.front().module_id == 0);
    CHECK_FALSE(shed.shedding_events.front().parent_module_id);
    CHECK_FALSE(shed.shedding_events.front().parent_terminal_node);
    const auto* shedding_events = shed.shedding_events.data();
    CHECK_FALSE(below->step(0.0F));
    CHECK(below->snapshot().shedding_events.data() == shedding_events);

    REQUIRE(below->step(1.0F));
    const auto dead = below->snapshot();
    CHECK(dead.plant_age == Catch::Approx(2.0F));
    CHECK(dead.modules.empty());
    CHECK(dead.shedding_events.empty());
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
    CHECK(std::ranges::count(attached.mature_terminals, TerminalAxisRole::Main,
                             &MatureTerminalSnapshot::axis_role) == 1);
    CHECK(std::ranges::count(attached.mature_terminals, TerminalAxisRole::Lateral,
                             &MatureTerminalSnapshot::axis_role) == 1);
    CHECK(module_by_id(attached, 1).root_position.x ==
          Catch::Approx(attached.mature_terminals[0].position.x));
    CHECK(module_by_id(attached, 1).root_position.z ==
          Catch::Approx(attached.mature_terminals[0].position.z));
    for (const auto& terminal : attached.mature_terminals) {
        REQUIRE(terminal.child_module_id);
        const auto& child = module_by_id(attached, *terminal.child_module_id);
        const float direction_alignment = child.transform.z_axis.x * terminal.tangent.x +
            child.transform.z_axis.y * terminal.tangent.y +
            child.transform.z_axis.z * terminal.tangent.z;
        CHECK(direction_alignment > 0.85F);
    }

    const auto child_transform = module_by_id(attached, 1).transform;
    REQUIRE(simulation->step(1.0F));
    const auto activated = simulation->snapshot();
    CHECK(activated.modules.size() == 3);
    CHECK(activated.attachment_events.empty());
    CHECK(module_by_id(activated, 1).physiological_age > 0.0F);
    CHECK(module_by_id(activated, 1).direct_light_exposure > 0.0F);
    CHECK(module_by_id(activated, 1).transform.z_axis.x == Catch::Approx(child_transform.z_axis.x));
    // Light accumulates over the module tree: the root carries its own direct
    // exposure plus every child's accumulated light.
    float children_light = 0.0F;
    for (const auto& module : activated.modules) {
        if (module.parent_module_id) {
            children_light += module.accumulated_light;
        }
    }
    CHECK(module_by_id(activated, 0).accumulated_light ==
          Catch::Approx(module_by_id(activated, 0).direct_light_exposure + children_light));
    // Vigor divides downward, so a child never exceeds the module feeding it.
    CHECK(module_by_id(activated, 0).vigor >= module_by_id(activated, 1).vigor);
}

TEST_CASE("same-step siblings avoid each other's mature bounds")
{
    using namespace toi::growth;
    auto plant_type = make_plant_type();
    plant_type.tropism_weight = 0.0F;
    auto simulation = PlantSimulation::create(make_colliding_sibling_library(), plant_type, 7);
    REQUIRE(simulation);
    REQUIRE(simulation->step(10'000.0F));
    const auto snapshot = simulation->snapshot();
    REQUIRE(snapshot.modules.size() == 3);
    const auto& first = module_by_id(snapshot, 1);
    const auto& second = module_by_id(snapshot, 2);
    const float axis_alignment = first.transform.z_axis.x * second.transform.z_axis.x +
        first.transform.z_axis.y * second.transform.z_axis.y +
        first.transform.z_axis.z * second.transform.z_axis.z;
    CHECK(axis_alignment < 0.99F);
}

TEST_CASE("same-step siblings orient from main terminal to least aligned lateral")
{
    using namespace toi::growth;
    auto simulation = PlantSimulation::create(make_unordered_terminal_library(), make_plant_type(), 7);
    REQUIRE(simulation);
    REQUIRE(simulation->step(10'000.0F));
    const auto snapshot = simulation->snapshot();
    REQUIRE(snapshot.attachment_events.size() == 3);
    CHECK(snapshot.attachment_events[0].parent_terminal_node == 2);
    CHECK(snapshot.attachment_events[1].parent_terminal_node == 4);
    CHECK(snapshot.attachment_events[2].parent_terminal_node == 3);
}

TEST_CASE("descendants attach in parent order with one cross-parent orientation batch")
{
    using namespace toi::growth;
    auto simulation = PlantSimulation::create(
        make_repeated_attachment_library(), make_repeated_attachment_plant_type(), 7);
    REQUIRE(simulation);

    REQUIRE(simulation->step(100'000.0F));
    auto first_generation = simulation->snapshot();
    REQUIRE(first_generation.modules.size() == 3);
    REQUIRE(first_generation.attachment_events.size() == 2);
    CHECK(first_generation.attachment_events[0].child_module_id == 1);
    CHECK(first_generation.attachment_events[1].child_module_id == 2);
    CHECK(first_generation.attachment_events[0].parent_module_id == 0);
    CHECK(first_generation.attachment_events[1].parent_module_id == 0);

    REQUIRE(simulation->step(100'000.0F));
    auto second_generation = simulation->snapshot();
    REQUIRE(second_generation.modules.size() == 5);
    REQUIRE(second_generation.attachment_events.size() == 2);
    CHECK(second_generation.attachment_events[0].child_module_id == 3);
    CHECK(second_generation.attachment_events[0].parent_module_id == 1);
    CHECK(second_generation.attachment_events[0].parent_terminal_node == 1);
    CHECK(second_generation.attachment_events[0].prototype_id == 8);
    CHECK(second_generation.attachment_events[1].child_module_id == 4);
    CHECK(second_generation.attachment_events[1].parent_module_id == 2);
    CHECK(second_generation.attachment_events[1].parent_terminal_node == 1);
    CHECK(second_generation.attachment_events[1].prototype_id == 6);
    CHECK(module_by_id(second_generation, 3).parent_module_id == 1);
    CHECK(module_by_id(second_generation, 3).parent_terminal_node == 1);
    CHECK(module_by_id(second_generation, 4).parent_module_id == 2);
    CHECK(module_by_id(second_generation, 4).parent_terminal_node == 1);
    const auto parent_segments = module_segments(second_generation, module_by_id(second_generation, 1));
    REQUIRE(parent_segments.size() == 1);
    CHECK(parent_segments.front().main_continuation_segment ==
          module_by_id(second_generation, 3).segments.offset);
    CHECK(module_by_id(second_generation, 3).physiological_age == Catch::Approx(0.0F));
    CHECK(module_by_id(second_generation, 4).physiological_age == Catch::Approx(0.0F));
    CHECK(module_by_id(second_generation, 3).vigor == Catch::Approx(0.0F));
    CHECK(module_by_id(second_generation, 4).vigor == Catch::Approx(0.0F));
    CHECK(direction_alignment(module_by_id(second_generation, 3).transform.z_axis,
                              only_terminal(second_generation, 1).tangent) > 0.999F);
    CHECK(direction_alignment(module_by_id(second_generation, 4).transform.z_axis,
                              only_terminal(second_generation, 2).tangent) < 0.99F);

    REQUIRE(simulation->step(100'000.0F));
    auto third_generation = simulation->snapshot();
    REQUIRE(third_generation.modules.size() == 7);
    REQUIRE(third_generation.attachment_events.size() == 2);
    CHECK(third_generation.attachment_events[0].child_module_id == 5);
    CHECK(third_generation.attachment_events[0].parent_module_id == 3);
    CHECK(third_generation.attachment_events[0].parent_terminal_node == 1);
    CHECK(third_generation.attachment_events[1].child_module_id == 6);
    CHECK(third_generation.attachment_events[1].parent_module_id == 4);
    CHECK(third_generation.attachment_events[1].parent_terminal_node == 1);

    REQUIRE(simulation->step(1.0F));
    const auto activated = simulation->snapshot();
    CHECK(activated.modules.size() == 7);
    CHECK(activated.attachment_events.empty());
}

TEST_CASE("below-threshold module sheds its descendant subtree before survivors grow")
{
    using namespace toi::growth;
    auto simulation = PlantSimulation::create(
        make_repeated_attachment_library(), make_shedding_plant_type(), 7);
    REQUIRE(simulation);
    REQUIRE(simulation->step(100'000.0F));
    REQUIRE(simulation->step(100'000.0F));
    const auto before = simulation->snapshot();
    REQUIRE(before.modules.size() == 5);
    const auto& removed_subtree_root = module_by_id(before, 2);
    CHECK(removed_subtree_root.vigor < kMinimumModuleVigor);
    CHECK(module_by_id(before, 4).parent_module_id == 2);
    REQUIRE(removed_subtree_root.parent_terminal_node);
    const auto removed_parent_terminal = *removed_subtree_root.parent_terminal_node;
    const auto before_root_segments = module_segments(before, module_by_id(before, 0));
    const auto supporting_segment = std::ranges::find(
        before_root_segments, std::optional<std::size_t>{removed_subtree_root.segments.offset},
        &PlantSegmentSnapshot::main_continuation_segment);
    REQUIRE(supporting_segment != before_root_segments.end());
    const auto supporting_source_segment = supporting_segment->source_segment_id;

    struct RetainedDiameter {
        std::size_t module_id;
        std::size_t source_segment_id;
        float diameter;
    };
    std::vector<RetainedDiameter> retained_diameters;
    for (const std::size_t module_id : {0U, 1U, 3U}) {
        const auto segments = module_segments(before, module_by_id(before, module_id));
        for (const auto& segment : segments) {
            retained_diameters.push_back({module_id, segment.source_segment_id, segment.diameter});
        }
    }

    REQUIRE(simulation->step(1.0F));
    const auto shed = simulation->snapshot();
    REQUIRE(shed.modules.size() == 3);
    CHECK(shed.modules[0].id == 0);
    CHECK(shed.modules[1].id == 1);
    CHECK(shed.modules[2].id == 3);
    CHECK(module_by_id(shed, 3).parent_module_id == 1);
    CHECK(std::ranges::none_of(shed.modules, [](const auto& module) {
        return module.id == 2 || module.id == 4;
    }));
    REQUIRE(shed.shedding_events.size() == 1);
    CHECK(shed.shedding_events.front().module_id == 2);
    CHECK(shed.shedding_events.front().parent_module_id == 0);
    CHECK(shed.shedding_events.front().parent_terminal_node == removed_parent_terminal);
    CHECK(shed.attachment_events.empty());

    const auto released_terminal = std::ranges::find_if(shed.mature_terminals, [&](const auto& terminal) {
        return terminal.module_id == 0 && terminal.terminal_node == removed_parent_terminal;
    });
    REQUIRE(released_terminal != shed.mature_terminals.end());
    CHECK_FALSE(released_terminal->child_module_id);
    const auto shed_root_segments = module_segments(shed, module_by_id(shed, 0));
    const auto exposed_segment = std::ranges::find(
        shed_root_segments, supporting_source_segment, &PlantSegmentSnapshot::source_segment_id);
    REQUIRE(exposed_segment != shed_root_segments.end());
    CHECK_FALSE(exposed_segment->main_continuation_segment);

    for (const auto& retained : retained_diameters) {
        const auto segments = module_segments(shed, module_by_id(shed, retained.module_id));
        const auto segment = std::ranges::find(
            segments, retained.source_segment_id, &PlantSegmentSnapshot::source_segment_id);
        REQUIRE(segment != segments.end());
        CHECK(segment->diameter >= retained.diameter);
    }
    check_recursive_light_and_vigor_conservation(shed);

    REQUIRE(simulation->step(100'000.0F));
    const auto continued = simulation->snapshot();
    CHECK(continued.shedding_events.empty());
    REQUIRE(continued.attachment_events.size() == 1);
    CHECK(continued.attachment_events.front().child_module_id == 5);
    CHECK(continued.attachment_events.front().parent_module_id == 3);
    CHECK(std::ranges::none_of(continued.modules, [](const auto& module) {
        return module.id == 2 || module.id == 4;
    }));
}

TEST_CASE("mature unoccupied terminal is reconsidered after its parent's maturity-crossing step")
{
    using namespace toi::growth;
    auto plant_type = make_plant_type();
    plant_type.root_max_vigor = 1.0F;
    plant_type.apical_control = 0.99F;
    plant_type.mature_apical_control = 0.5F;
    plant_type.flowering_age = 1.0F;
    auto simulation = PlantSimulation::create(make_library(), plant_type, 7);
    REQUIRE(simulation);

    REQUIRE(simulation->step(10'000.0F));
    const auto crossed = simulation->snapshot();
    REQUIRE(crossed.modules.size() == 2);
    REQUIRE(crossed.attachment_events.size() == 1);
    const auto unattached = std::ranges::find_if(crossed.mature_terminals, [](const auto& terminal) {
        return terminal.module_id == 0 && !terminal.child_module_id;
    });
    REQUIRE(unattached != crossed.mature_terminals.end());
    CHECK(unattached->vigor > kMinimumModuleVigor);
    const std::size_t unattached_node = unattached->terminal_node;

    REQUIRE(simulation->step(1.0F));
    const auto later = simulation->snapshot();
    CHECK(later.modules.size() == 3);
    REQUIRE(later.attachment_events.size() == 1);
    CHECK(later.attachment_events.front().child_module_id == 2);
    CHECK(later.attachment_events.front().parent_module_id == 0);
    CHECK(later.attachment_events.front().parent_terminal_node == unattached_node);
    const auto reconsidered = std::ranges::find_if(later.mature_terminals, [&](const auto& terminal) {
        return terminal.module_id == 0 && terminal.terminal_node == unattached_node;
    });
    REQUIRE(reconsidered != later.mature_terminals.end());
    CHECK(reconsidered->child_module_id == 2);
    CHECK(reconsidered->vigor > kMinimumModuleVigor);
}

TEST_CASE("continuous pipe crosses parent and child module attachment")
{
    using namespace toi::growth;
    const auto plant_type = make_plant_type();
    auto simulation = PlantSimulation::create(make_library(), plant_type, 7);
    REQUIRE(simulation);
    REQUIRE(simulation->step(10'000.0F));
    const auto snapshot = simulation->snapshot();
    const auto& root = module_by_id(snapshot, 0);
    const auto& child = module_by_id(snapshot, 1);
    const auto root_segments = module_segments(snapshot, root);
    const auto child_segments = module_segments(snapshot, child);
    CHECK(child_segments[0].diameter == Catch::Approx(plant_type.terminal_thickness * 0.01F));
    CHECK(root_segments[1].diameter == Catch::Approx(child_segments[0].diameter));
    CHECK(root_segments[0].diameter * root_segments[0].diameter ==
          Catch::Approx(root_segments[1].diameter * root_segments[1].diameter +
                        root_segments[2].diameter * root_segments[2].diameter));
    CHECK(root_segments[0].main_continuation_segment == root.segments.offset + 1);
    CHECK(root_segments[1].main_continuation_segment == child.segments.offset);
    CHECK(root_segments[2].main_continuation_segment == module_by_id(snapshot, 2).segments.offset);

    std::vector<float> previous_diameters;
    previous_diameters.reserve(snapshot.segments.size());
    for (const auto& segment : snapshot.segments) previous_diameters.push_back(segment.diameter);
    const float attachment_diameter = root_segments[1].diameter;
    for (int step = 0; step < 3; ++step) {
        REQUIRE(simulation->step(1.0F));
        const auto growing = simulation->snapshot();
        REQUIRE(growing.segments.size() == previous_diameters.size());
        for (std::size_t segment = 0; segment < growing.segments.size(); ++segment) {
            CHECK(growing.segments[segment].diameter >= previous_diameters[segment]);
            previous_diameters[segment] = growing.segments[segment].diameter;
        }
        const auto growing_root_segments = module_segments(growing, module_by_id(growing, 0));
        const auto growing_child_segments = module_segments(growing, module_by_id(growing, 1));
        CHECK(growing_root_segments[1].diameter == Catch::Approx(growing_child_segments[0].diameter));
    }
    const auto final_snapshot = simulation->snapshot();
    CHECK(module_segments(final_snapshot, module_by_id(final_snapshot, 0))[1].diameter > attachment_diameter);
}

TEST_CASE("acropetal vigor flux is conserved across module attachments")
{
    using namespace toi::growth;
    auto plant_type = make_plant_type();
    plant_type.root_max_vigor = 2.0F;
    auto simulation = PlantSimulation::create(make_library(), plant_type, 7);
    REQUIRE(simulation);
    REQUIRE(simulation->step(10'000.0F));
    REQUIRE(simulation->step(1.0F));
    const auto snapshot = simulation->snapshot();

    // The root carries its whole budget; maximum module vigor does not truncate it.
    const auto& root = module_by_id(snapshot, 0);
    CHECK(root.vigor == Catch::Approx(2.0F));
    CHECK(root.vigor > kMaximumModuleVigor);

    // Vigor above the maximum survives a module junction rather than being discarded.
    CHECK(module_by_id(snapshot, 1).vigor > kMaximumModuleVigor);

    // Eq. 2 conserves: the children of a module receive exactly its vigor.
    float distributed = 0.0F;
    for (const auto& module : snapshot.modules) {
        if (module.parent_module_id && *module.parent_module_id == 0) {
            distributed += module.vigor;
        }
    }
    CHECK(distributed == Catch::Approx(root.vigor));
}

TEST_CASE("maximum module vigor bounds the growth rate, not the propagated flux")
{
    using namespace toi::growth;
    const auto plant_type = make_plant_type();
    const auto at_maximum = growth_rate(plant_type, {.vigor = kMaximumModuleVigor,
                                                     .min_vigor = kMinimumModuleVigor,
                                                     .max_vigor = kMaximumModuleVigor});
    const auto above_maximum = growth_rate(plant_type, {.vigor = 9.9F,
                                                        .min_vigor = kMinimumModuleVigor,
                                                        .max_vigor = kMaximumModuleVigor});
    REQUIRE(at_maximum);
    REQUIRE(above_maximum);
    CHECK(*above_maximum == Catch::Approx(*at_maximum));

    // Paper: D' = v̄(u) D / v̄_max normalizes into [0, D], so raw vigor above the
    // maximum must not scale determinacy past its unscaled value.
    CHECK(vigor_scaled_determinacy(0.4F, 9.9F) == Catch::Approx(0.4F));
    CHECK(vigor_scaled_determinacy(0.4F, 0.5F) == Catch::Approx(0.2F));
}

TEST_CASE("repeated plant development conserves light and vigor and remains deterministic")
{
    using namespace toi::growth;
    auto first = PlantSimulation::create(
        make_repeated_attachment_library(), make_repeated_attachment_plant_type(), 7);
    auto second = PlantSimulation::create(
        make_repeated_attachment_library(), make_repeated_attachment_plant_type(), 7);
    REQUIRE(first);
    REQUIRE(second);

    for (const float timestep : {100'000.0F, 100'000.0F, 100'000.0F, 1.0F}) {
        REQUIRE(first->step(timestep));
        REQUIRE(second->step(timestep));
        check_snapshots_equal(first->snapshot(), second->snapshot());
    }

    const auto snapshot = first->snapshot();
    REQUIRE(snapshot.modules.size() == 7);
    CHECK(snapshot.attachment_events.empty());
    check_recursive_light_and_vigor_conservation(snapshot);
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
