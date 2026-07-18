#pragma once

#include "toi/render/orbit_view.hpp"
#include "toi/render/render_projection.hpp"
#include "toi/viewport/cuda_vulkan_interop.hpp"
#include "toi/viewport/error.hpp"
#include "toi/viewport/vulkan_context.hpp"
#include "toi/viewport/viewport_overlay.hpp"

#include <vulkan/vulkan.h>

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace toi::ovrtx {
class RendererSession;
}

namespace toi::viewport {

struct PreviewRendererDevice {
    VkPhysicalDevice physical_device = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    std::uint32_t queue_family = 0;
    int cuda_device = 0;
    std::string device_name;
};

struct ProjectedPlantDiagnosticLabel {
    std::size_t module_id = 0;
    float x = 0.0F;
    float y = 0.0F;
    bool visible = false;
    float direct_light_exposure = 0.0F;
    float accumulated_light = 0.0F;
    float vigor = 0.0F;
};

struct PreviewRendererStatus {
    std::string phase = "starting";
    std::string message = "Qt Vulkan viewport starting";
    int width = 0;
    int height = 0;
    std::uint64_t frame_generation = 0;
    std::uint64_t scene_frame_count = 0;
    std::uint64_t precomposition_count = 0;
};

class PreviewRenderer final {
public:
    [[nodiscard]] static Result<std::unique_ptr<PreviewRenderer>> create(
        PreviewRendererDevice device, render::GrowthPreviewStageProjection initial_stage,
        render::OrbitView initial_orbit);

    PreviewRenderer(const PreviewRenderer&) = delete;
    PreviewRenderer& operator=(const PreviewRenderer&) = delete;
    ~PreviewRenderer();

    void set_stage(render::GrowthPreviewStageProjection stage, render::OrbitView orbit);
    void set_orbit(render::OrbitView orbit);
    [[nodiscard]] std::optional<render::OrbitView> apply_camera_input(std::string_view kind, float dx, float dy,
                                                                      int viewport_height);
    void set_guide_options(bool guides_visible, bool world_origin_axes_visible);
    void set_frame_ready_callback(std::function<void()> callback);
    void set_diagnostic_labels_callback(
        std::function<void(std::vector<ProjectedPlantDiagnosticLabel>)> callback);

    // Called only from Qt's scene-graph render thread. Enqueues native
    // precomposition before Qt submits the frame that samples display_image().
    [[nodiscard]] bool prepare_frame_on_render_thread();

    [[nodiscard]] VkImage display_image() const;
    [[nodiscard]] VkImageLayout display_layout() const;
    [[nodiscard]] int width() const;
    [[nodiscard]] int height() const;
    [[nodiscard]] int texture_width() const;
    [[nodiscard]] int texture_height() const;
    [[nodiscard]] PreviewRendererStatus status() const;

private:
    struct Slot;

    PreviewRenderer() = default;
    [[nodiscard]] Result<void> initialize(PreviewRendererDevice device,
                                          render::GrowthPreviewStageProjection initial_stage,
                                          render::OrbitView initial_orbit);
    void render_loop();
    [[nodiscard]] Result<void> resize_frame_resources_on_render_thread(int width, int height);
    void set_error(std::string message);

    VulkanContext context_;
    std::unique_ptr<ovrtx::RendererSession> renderer_;
    std::unique_ptr<Slot[]> slots_;
    CudaInteropTimelineSemaphore frames_ready_;
    ViewportOverlay overlay_;
    VkCommandPool command_pool_ = VK_NULL_HANDLE;
    VkImage display_image_ = VK_NULL_HANDLE;
    VkDeviceMemory display_memory_ = VK_NULL_HANDLE;
    int width_ = 0;
    int height_ = 0;
    int presented_width_ = 0;
    int presented_height_ = 0;

    std::atomic<bool> running_{false};
    std::thread thread_;
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::optional<render::GrowthPreviewStageProjection> pending_stage_;
    bool dirty_ = false;
    bool stage_dirty_ = false;
    bool camera_dirty_ = false;
    bool resize_waiting_ = false;
    int requested_width_ = 0;
    int requested_height_ = 0;
    bool guides_visible_ = true;
    bool world_origin_axes_visible_ = true;
    std::optional<render::GrowthPreviewCamera> base_camera_;
    std::optional<render::OrbitView> orbit_;
    int ready_slot_ = -1;
    int displayed_slot_ = -1;
    std::uint64_t next_timeline_value_ = 0;
    std::function<void()> frame_ready_callback_;
    std::function<void(std::vector<ProjectedPlantDiagnosticLabel>)> diagnostic_labels_callback_;
    PreviewRendererStatus status_;
};

} // namespace toi::viewport
