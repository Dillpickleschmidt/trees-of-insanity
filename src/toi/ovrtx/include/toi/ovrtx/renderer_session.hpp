#pragma once

#include "toi/ovrtx/cuda_resources.hpp"
#include "toi/ovrtx/handles.hpp"
#include "toi/ovrtx/output_mapping.hpp"
#include "toi/render/render_projection.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>

namespace toi::ovrtx {

struct RendererSessionOptions {
    std::string active_cuda_gpus = "0";
    int cuda_device = 0;
    double delta_seconds = 1.0 / 60.0;
    std::filesystem::path asset_search_path;
};

enum class RenderFrameOutputs {
    Color,
    ColorAndDistance,
};

struct MappedCudaFrame {
    ResultsHandle results;
    MappedOutputHandle output;
    MappedOutputHandle distance_output;
    const DLTensor* tensor = nullptr;
    int width = 0;
    int height = 0;
    int channel_count = 0;
    std::size_t row_stride_bytes = 0;
    std::size_t byte_offset = 0;
    std::uintptr_t sync_stream = 1;
    render::GrowthPreviewCamera camera;
    const void* scene_distance_cuda_array = nullptr;
};

class RendererSession {
public:
    [[nodiscard]] static Result<RendererSession> create(RendererSessionOptions options = {});

    RendererSession(const RendererSession&) = delete;
    RendererSession& operator=(const RendererSession&) = delete;
    RendererSession(RendererSession&&) noexcept = default;
    RendererSession& operator=(RendererSession&&) noexcept = delete;
    ~RendererSession();

    [[nodiscard]] Result<void> submit_growth_preview(const render::GrowthPreviewStageProjection& stage);
    [[nodiscard]] Result<void> set_growth_preview_camera(render::GrowthPreviewCamera camera);
    [[nodiscard]] Result<MappedCudaFrame> render_cuda_frame(RenderFrameOutputs outputs = RenderFrameOutputs::Color,
                                                            std::uintptr_t sync_stream = 0);

private:
    RendererSession(RendererHandle renderer, CudaStream stream, RendererSessionOptions options);

    [[nodiscard]] Result<MappedCudaFrame> render_cuda_frame_once(RenderFrameOutputs outputs,
                                                                 std::uintptr_t sync_stream);

    RendererHandle renderer_;
    CudaStream stream_;
    RendererSessionOptions options_;
    std::string loaded_stage_usda_;
    int pending_warmup_frames_ = 0;
    std::string render_product_path_ = "/OVRenderProduct";
    std::string camera_path_ = "/OVCamera";
    render::GrowthPreviewCamera camera_;
};

} // namespace toi::ovrtx
