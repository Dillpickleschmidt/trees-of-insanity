#pragma once

#include "toi/render/render_projection.hpp"

#include <vector>

namespace toi::render {

struct ViewportGuideLine {
    growth::Vec3 start;
    growth::Vec3 end;
    growth::Vec3 color;
    float alpha;
    float width_pixels;
};

struct ViewportGuideOverlay {
    std::vector<ViewportGuideLine> lines;
};

[[nodiscard]] ViewportGuideOverlay make_growth_preview_guides(const GrowthPreviewCamera& camera);

} // namespace toi::render
