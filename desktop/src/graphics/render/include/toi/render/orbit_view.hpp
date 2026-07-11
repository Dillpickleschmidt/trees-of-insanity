#pragma once

#include "toi/render/render_projection.hpp"

namespace toi::render {

struct OrbitView {
    growth::Vec3 target;
    float radius = 1.0F;
    float azimuth_radians = 0.0F;
    float elevation_radians = 0.0F;
};

[[nodiscard]] OrbitView orbit_view_from_camera(const GrowthPreviewCamera& camera);
[[nodiscard]] OrbitView rotate_orbit_view(OrbitView view, float azimuth_delta_radians, float elevation_delta_radians);
[[nodiscard]] OrbitView dolly_orbit_view(OrbitView view, float radius_multiplier);
[[nodiscard]] GrowthPreviewCamera apply_orbit_view(GrowthPreviewCamera camera, const OrbitView& view);

} // namespace toi::render
