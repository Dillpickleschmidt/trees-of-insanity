#include "toi/render/orbit_view.hpp"

#include <algorithm>
#include <cmath>

namespace toi::render {
namespace {

constexpr float kPi = 3.14159265358979323846F;
constexpr float kHalfPi = 0.5F * kPi;
constexpr float kMaxElevationRadians = kHalfPi - 0.01F;
constexpr float kMinRadius = 1.0e-3F;
constexpr float kMaxRadius = 1.0e6F;
constexpr float kEpsilon = 1.0e-6F;

[[nodiscard]] growth::Vec3 cross(growth::Vec3 left, growth::Vec3 right);
[[nodiscard]] growth::Vec3 safe_normalize(growth::Vec3 value, growth::Vec3 fallback);
[[nodiscard]] float clamped_elevation(float elevation_radians);
[[nodiscard]] float clamped_radius(float radius);

} // namespace

OrbitView orbit_view_from_camera(const GrowthPreviewCamera& camera)
{
    const auto offset = growth::subtract(camera.eye, camera.target);
    const float radius = clamped_radius(growth::length(offset));
    return {
        .target = camera.target,
        .radius = radius,
        .azimuth_radians = std::atan2(offset.y, offset.x),
        .elevation_radians = clamped_elevation(std::asin(std::clamp(offset.z / radius, -1.0F, 1.0F))),
    };
}

OrbitView rotate_orbit_view(OrbitView view, float azimuth_delta_radians, float elevation_delta_radians)
{
    view.azimuth_radians += azimuth_delta_radians;
    view.elevation_radians = clamped_elevation(view.elevation_radians + elevation_delta_radians);
    view.radius = clamped_radius(view.radius);
    return view;
}

OrbitView dolly_orbit_view(OrbitView view, float radius_multiplier)
{
    if (!std::isfinite(radius_multiplier) || radius_multiplier <= 0.0F) {
        radius_multiplier = 1.0F;
    }
    view.radius = clamped_radius(view.radius * radius_multiplier);
    return view;
}

GrowthPreviewCamera apply_orbit_view(GrowthPreviewCamera camera, const OrbitView& view)
{
    const float radius = clamped_radius(view.radius);
    const float elevation = clamped_elevation(view.elevation_radians);
    const float horizontal_radius = radius * std::cos(elevation);
    const growth::Vec3 offset{
        .x = horizontal_radius * std::cos(view.azimuth_radians),
        .y = horizontal_radius * std::sin(view.azimuth_radians),
        .z = radius * std::sin(elevation),
    };
    const growth::Vec3 eye = growth::add(view.target, offset);
    const growth::Vec3 world_up{.x = 0.0F, .y = 0.0F, .z = 1.0F};
    const growth::Vec3 forward = safe_normalize(growth::subtract(view.target, eye), {.x = 0.0F, .y = 1.0F, .z = 0.0F});
    const growth::Vec3 right = safe_normalize(cross(forward, world_up), {.x = 1.0F, .y = 0.0F, .z = 0.0F});
    const growth::Vec3 up = safe_normalize(cross(right, forward), world_up);

    camera.eye = eye;
    camera.target = view.target;
    camera.right = right;
    camera.up = up;
    camera.negative_forward = growth::scale(forward, -1.0F);
    return camera;
}

namespace {

[[nodiscard]] growth::Vec3 cross(growth::Vec3 left, growth::Vec3 right)
{
    return {
        .x = left.y * right.z - left.z * right.y,
        .y = left.z * right.x - left.x * right.z,
        .z = left.x * right.y - left.y * right.x,
    };
}

[[nodiscard]] growth::Vec3 safe_normalize(growth::Vec3 value, growth::Vec3 fallback)
{
    return growth::length(value) <= kEpsilon ? fallback : growth::normalize(value);
}

[[nodiscard]] float clamped_elevation(float elevation_radians)
{
    if (!std::isfinite(elevation_radians)) {
        return 0.0F;
    }
    return std::clamp(elevation_radians, -kMaxElevationRadians, kMaxElevationRadians);
}

[[nodiscard]] float clamped_radius(float radius)
{
    if (!std::isfinite(radius)) {
        return kMinRadius;
    }
    return std::clamp(radius, kMinRadius, kMaxRadius);
}

} // namespace
} // namespace toi::render
