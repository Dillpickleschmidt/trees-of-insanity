#include "toi/growth/growth.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>

namespace toi::growth {
namespace {

[[nodiscard]] float sphere_volume(float radius);
[[nodiscard]] float sphere_overlap_volume(float center_distance, float first_radius, float second_radius);
[[nodiscard]] float dot(Vec3 left, Vec3 right);

} // namespace

float collision_measure(const Sphere& own_sphere, std::span<const Sphere> other_spheres)
{
    if (own_sphere.radius <= 0.0F) {
        return 0.0F;
    }

    float intersection_volume = 0.0F;
    for (const Sphere& other : other_spheres) {
        if (other.radius > 0.0F) {
            intersection_volume +=
                sphere_overlap_volume(distance(own_sphere.center, other.center), own_sphere.radius, other.radius);
        }
    }
    // Paper: f_collisions(u) = sum_w V_intersect(B_u, B_w).
    return intersection_volume;
}

float light_exposure(float collision_measure_value)
{
    // Paper: Q(u) = exp(-f_collisions(u)).
    return std::exp(-collision_measure_value);
}

VigorSplit split_vigor(float available_vigor, float main_axis_light, float lateral_axis_light, float apical_control)
{
    // Paper: v̄(u_m) = v̄(u) lambda Q(u_m) / (lambda Q(u_m) + (1-lambda) Q(u_l)).
    const float main_weight = apical_control * main_axis_light;
    const float lateral_weight = (1.0F - apical_control) * lateral_axis_light;
    const float denominator = main_weight + lateral_weight;
    const float main_vigor = denominator <= kEpsilon ? available_vigor * apical_control
                                                      : available_vigor * main_weight / denominator;
    return {.main_axis = main_vigor, .lateral_axis = available_vigor - main_vigor};
}

float vigor_scaled_determinacy(float determinacy, float module_vigor)
{
    // Paper: D' = v̄(u) D / v̄_max, with shared module v̄_max = 1.
    return module_vigor * determinacy / kMaximumModuleVigor;
}

std::size_t nearest_morphospace_prototype(float apical_control, float determinacy)
{
    static constexpr std::array<float, 3> levels{1.0F / 6.0F, 0.5F, 5.0F / 6.0F};
    std::size_t nearest = 0;
    float nearest_distance = std::numeric_limits<float>::max();
    for (std::size_t index = 0; index < 9; ++index) {
        const float apical_delta = apical_control - levels[index / 3];
        const float determinacy_delta = determinacy - levels[index % 3];
        const float squared_distance = apical_delta * apical_delta + determinacy_delta * determinacy_delta;
        if (squared_distance < nearest_distance) {
            nearest_distance = squared_distance;
            nearest = index;
        }
    }
    return nearest;
}

float parameter_for_plant_age(float young_value, std::optional<float> mature_value, float flowering_age,
                              float plant_age)
{
    return mature_value && flowering_age > 0.0F && plant_age >= flowering_age ? *mature_value : young_value;
}

float orientation_tropism_cost(float tropism_angle, Vec3 module_axis, Vec3 tropism_axis)
{
    const float axis_cosine = dot(normalize(module_axis), normalize(tropism_axis));
    // Paper: f_tropism = |cos(alpha_tropism) - cos(module axis, tropism axis)|.
    return std::fabs(std::cos(tropism_angle) - axis_cosine);
}

float orientation_distribution_cost(float collision_measure_value, float collision_weight, float tropism_cost,
                                    float tropism_weight)
{
    // Paper: f_distribution = omega_1 f_collisions + omega_2 f_tropism.
    return collision_weight * collision_measure_value + tropism_weight * tropism_cost;
}

float physiological_age_euler_step(float physiological_age, float growth_rate_value, float time_step)
{
    // Paper: a_u(t + delta_t) = a_u(t) + Upsilon(u) delta_t.
    return physiological_age + growth_rate_value * time_step;
}

namespace {

float sphere_volume(float radius)
{
    return 4.0F / 3.0F * std::numbers::pi_v<float> * radius * radius * radius;
}

float sphere_overlap_volume(float center_distance, float first_radius, float second_radius)
{
    if (center_distance >= first_radius + second_radius) {
        return 0.0F;
    }
    if (center_distance <= std::fabs(first_radius - second_radius)) {
        return sphere_volume(std::min(first_radius, second_radius));
    }

    const float radius_sum = first_radius + second_radius;
    const float radius_difference = first_radius - second_radius;
    return std::numbers::pi_v<float> * (radius_sum - center_distance) * (radius_sum - center_distance) *
           (center_distance * center_distance + 2.0F * center_distance * radius_sum -
            3.0F * radius_difference * radius_difference) /
           (12.0F * center_distance);
}

float dot(Vec3 left, Vec3 right)
{
    return left.x * right.x + left.y * right.y + left.z * right.z;
}

} // namespace
} // namespace toi::growth
