#include "toi/growth/growth.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace toi::growth {
namespace {

[[nodiscard]] GrowthError invalid_input(std::string message);
[[nodiscard]] GrowthError invalid_prototype(std::string message);
[[nodiscard]] Sphere collision_sphere_for(const BranchModulePrototype& prepared_root,
                                          std::span<const GrowthSnapshotSegment> segments);

} // namespace

Result<PlantSimulation> PlantSimulation::create(const BranchModulePrototypeLibrary& prototype_library,
                                                const PlantTypeParameters& plant_type,
                                                std::size_t root_prototype_id)
{
    if (!plant_type_parameters_are_valid(plant_type)) {
        return std::unexpected(invalid_input("plant type parameters are invalid"));
    }
    const auto found = std::ranges::find_if(
        prototype_library.prototypes,
        [root_prototype_id](const BranchModulePrototype& prototype) { return prototype.id == root_prototype_id; });
    if (found == prototype_library.prototypes.end()) {
        return std::unexpected(invalid_prototype("root prototype does not exist"));
    }

    auto prepared_root = prepare_branch_module_prototype(*found, plant_type);
    if (!prepared_root) {
        return std::unexpected(prepared_root.error());
    }
    auto mature_age = fully_grown_age(*prepared_root, plant_type);
    if (!mature_age) {
        return std::unexpected(mature_age.error());
    }

    PlantSimulation simulation;
    simulation.plant_type_ = plant_type;
    simulation.prepared_root_ = std::move(*prepared_root);
    simulation.fully_grown_age_ = *mature_age;
    auto rebuilt = simulation.rebuild_snapshot(0.0F, 0.0F);
    if (!rebuilt) {
        return std::unexpected(rebuilt.error());
    }
    return simulation;
}

Result<void> PlantSimulation::step(float timestep)
{
    if (!std::isfinite(timestep) || timestep <= 0.0F) {
        return std::unexpected(invalid_input("plant timestep must be finite and positive"));
    }
    const float next_plant_age = plant_age_ + timestep;
    if (!std::isfinite(next_plant_age)) {
        return std::unexpected(invalid_input("plant age must remain finite"));
    }

    const float current_growth_rate = modules_.front().growth_rate;
    // Paper Eq. 6: a_u(t + Δt) = a_u(t) + Υ(u)Δt.
    const float next_physiological_age =
        std::min(fully_grown_age_, physiological_age_euler_step(root_physiological_age_, current_growth_rate, timestep));
    return rebuild_snapshot(next_plant_age, next_physiological_age);
}

PlantSnapshot PlantSimulation::snapshot() const
{
    return {.plant_age = plant_age_, .modules = modules_};
}

Result<void> PlantSimulation::rebuild_snapshot(float plant_age, float root_physiological_age)
{
    auto geometry = make_growth_snapshot(prepared_root_, plant_type_, root_physiological_age);
    if (!geometry) {
        return std::unexpected(geometry.error());
    }

    const Vec3 root_offset = scale(prepared_root_.nodes[prepared_root_.root_node].position, -1.0F);
    for (auto& segment : geometry->segments) {
        segment.parent_position = add(segment.parent_position, root_offset);
        segment.child_position = add(segment.child_position, root_offset);
    }

    const Sphere collision_sphere = collision_sphere_for(prepared_root_, geometry->segments);
    // Paper Eq. 1: the root-only plant has no other module collision spheres.
    const float direct_light = light_exposure(collision_measure(collision_sphere, std::span<const Sphere>{}));
    const float accumulated_light = direct_light;
    const float root_vigor_budget = std::min(accumulated_light, plant_type_.root_max_vigor);
    const float module_vigor = std::min(root_vigor_budget, kMaximumModuleVigor);
    auto module_growth_rate =
        growth_rate(plant_type_, {.vigor = module_vigor,
                                  .min_vigor = kMinimumModuleVigor,
                                  .max_vigor = kMaximumModuleVigor});
    if (!module_growth_rate) {
        return std::unexpected(module_growth_rate.error());
    }

    plant_age_ = plant_age;
    root_physiological_age_ = root_physiological_age;
    root_segments_ = std::move(geometry->segments);
    modules_.clear();
    modules_.push_back({
        .id = 0,
        .prototype_id = prepared_root_.id,
        .root_position = {},
        .physiological_age = root_physiological_age_,
        .fully_grown_age = fully_grown_age_,
        .direct_light_exposure = direct_light,
        .accumulated_light = accumulated_light,
        .vigor = module_vigor,
        .growth_rate = *module_growth_rate,
        .collision_sphere = collision_sphere,
        .segments = root_segments_,
    });
    return {};
}

namespace {

GrowthError invalid_input(std::string message)
{
    return {.code = GrowthError::Code::InvalidInput, .message = std::move(message)};
}

GrowthError invalid_prototype(std::string message)
{
    return {.code = GrowthError::Code::InvalidPrototype, .message = std::move(message)};
}

Sphere collision_sphere_for(const BranchModulePrototype& prepared_root,
                            std::span<const GrowthSnapshotSegment> segments)
{
    std::vector<Vec3> developed_nodes;
    developed_nodes.reserve(segments.size() + 1);
    developed_nodes.push_back({});

    std::vector<bool> included(prepared_root.nodes.size(), false);
    included[prepared_root.root_node] = true;
    for (const auto& segment : segments) {
        const std::size_t child_node = prepared_root.segments[segment.source_segment_id].child_node;
        if (!included[child_node]) {
            included[child_node] = true;
            developed_nodes.push_back(segment.child_position);
        }
    }

    Vec3 center;
    for (const auto point : developed_nodes) {
        center = add(center, point);
    }
    center = scale(center, 1.0F / static_cast<float>(developed_nodes.size()));

    float radius = 0.0F;
    for (const auto point : developed_nodes) {
        radius = std::max(radius, distance(center, point));
    }
    return {.center = center, .radius = radius};
}

} // namespace
} // namespace toi::growth
