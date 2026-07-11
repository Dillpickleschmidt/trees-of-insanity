#include "toi/render/viewport_guides.hpp"

#include <algorithm>

namespace toi::render {
namespace {

constexpr float kReferenceAxisVisibleHeightRatio = 8.0F;
constexpr float kReferenceAxisWidthPixels = 0.5F;
constexpr float kEpsilon = 1.0e-6F;

[[nodiscard]] float visible_height(const GrowthPreviewCamera& camera)
{
    const float focus_distance = growth::distance(camera.eye, camera.target);
    const auto height =
        static_cast<float>(static_cast<double>(focus_distance) * camera.vertical_aperture / camera.focal_length);
    return std::max(kEpsilon, height);
}

} // namespace

ViewportGuideOverlay make_growth_preview_guides(const GrowthPreviewCamera& camera)
{
    const float axis_length = visible_height(camera) * kReferenceAxisVisibleHeightRatio;
    return {
        .lines =
            {
                {
                    .start = {.x = -axis_length, .y = 0.0F, .z = 0.0F},
                    .end = {.x = axis_length, .y = 0.0F, .z = 0.0F},
                    .color = {.x = 1.0F, .y = 0.2F, .z = 0.3216F},
                    .alpha = 1.0F,
                    .width_pixels = kReferenceAxisWidthPixels,
                },
                {
                    .start = {.x = 0.0F, .y = -axis_length, .z = 0.0F},
                    .end = {.x = 0.0F, .y = axis_length, .z = 0.0F},
                    .color = {.x = 0.5451F, .y = 0.8627F, .z = 0.0F},
                    .alpha = 1.0F,
                    .width_pixels = kReferenceAxisWidthPixels,
                },
            },
    };
}

} // namespace toi::render
