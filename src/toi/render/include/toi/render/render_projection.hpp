#pragma once

#include "toi/growth/growth.hpp"

#include <array>
#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace toi::render {

struct GrowthPreviewStageOptions {
    int width = 1280;
    int height = 720;
    std::filesystem::path asset_search_path;
    std::filesystem::path hdri_texture_path = "HDRI/meadow_2_4k.exr";
    // When false the dome light keeps illuminating the scene but is hidden
    // from camera rays, so the HDRI no longer shows as a backdrop.
    bool hdri_visible = true;
};

struct GrowthPreviewUsdStage {
    std::string text;
    std::string render_product_path;
    std::string camera_path;
    std::filesystem::path asset_search_path;
    std::filesystem::path hdri_texture_path;
    int width = 0;
    int height = 0;
};

struct GrowthPreviewMeshAttributes {
    std::string prim_path;
    std::vector<growth::Vec3> points;
};

struct GrowthPreviewMeshStats {
    std::size_t chain_count = 0;
    std::size_t mesh_count = 0;
    std::size_t vertex_count = 0;
    std::size_t face_count = 0;
};

struct GrowthPreviewCamera {
    growth::Vec3 eye;
    growth::Vec3 target;
    growth::Vec3 right;
    growth::Vec3 up;
    growth::Vec3 negative_forward;
    double focal_length = 0.0;
    double horizontal_aperture = 0.0;
    double vertical_aperture = 0.0;
    float near_clip = 0.1F;
    float far_clip = 100000.0F;
    int width = 0;
    int height = 0;
};

struct GrowthPreviewStageProjection {
    GrowthPreviewUsdStage usd_stage;
    GrowthPreviewMeshStats mesh;
    GrowthPreviewCamera camera;
    std::vector<GrowthPreviewMeshAttributes> mesh_attributes;
};

[[nodiscard]] GrowthPreviewStageProjection make_growth_preview_stage_projection(
    const growth::GrowthSnapshot& snapshot, const growth::GrowthSnapshot& camera_snapshot,
    const growth::BranchModulePrototype& prepared_prototype, GrowthPreviewStageOptions options = {});
[[nodiscard]] std::array<double, 16> growth_preview_camera_transform_matrix(const GrowthPreviewCamera& camera);

} // namespace toi::render
