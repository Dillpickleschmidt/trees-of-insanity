#pragma once

#include "toi/viewport/error.hpp"
#include "toi/viewport/vulkan_context.hpp"
#include "toi/viewport/vulkan_swapchain.hpp"
#include "toi/viewport/x11_vulkan_surface.hpp"

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#ifdef TOI_ENABLE_OVRTX
#include "toi/ovrtx/renderer_session.hpp"
#include "toi/render/orbit_view.hpp"
#include "toi/viewport/cuda_vulkan_interop.hpp"
#include "toi/viewport/viewport_overlay.hpp"

#include <mutex>
#include <optional>
#endif

namespace toi::viewport {

struct ViewportInfo {
    std::string device_name;
    int width = 0;
    int height = 0;
};

// Owns a Vulkan presentation loop targeting the shell's native X11 window.
// Without ovrtx it presents an animated clear color (a viewport test pattern).
// With ovrtx it presents the growth preview: each frame the ovrtx CUDA color
// output is copied into a shared Vulkan image and blitted to the swapchain.
class ViewportSession {
public:
    [[nodiscard]] static Result<std::unique_ptr<ViewportSession>> attach(unsigned long x_window, int width,
                                                                         int height);

    ViewportSession(const ViewportSession&) = delete;
    ViewportSession& operator=(const ViewportSession&) = delete;
    ~ViewportSession();

    [[nodiscard]] const ViewportInfo& info() const;

#ifdef TOI_ENABLE_OVRTX
    // Hand the render thread the latest growth-preview stage (built by the
    // command thread when a preview-changing command runs).
    void set_pending_stage(render::GrowthPreviewStageProjection stage);
    void set_guide_options(bool guides_visible, bool world_origin_axes_visible);
#endif

private:
    // One frame in flight: the top-of-loop fence wait then guarantees the prior
    // frame's blit finished before the next CUDA copy reuses the single shared
    // interop image and semaphore. The ovrtx render dominates frame time, so
    // this costs no real throughput.
    static constexpr int kFramesInFlight = 1;

    ViewportSession() = default;

    [[nodiscard]] Result<void> create_sync_objects();
    [[nodiscard]] Result<void> create_swapchain_resources();
    void destroy_swapchain_resources();
    [[nodiscard]] Result<void> recreate_swapchain();
    void render_loop();
    void record_test_pattern(VkCommandBuffer command_buffer, VkImage image, std::uint64_t frame);

    X11Connection connection_;
    VulkanContext context_;
    VulkanSwapchain swapchain_;
    unsigned long x_window_ = 0;

    VkSemaphore image_available_[kFramesInFlight]{};
    VkFence in_flight_[kFramesInFlight]{};
    std::vector<VkSemaphore> render_finished_;
    std::vector<VkCommandBuffer> command_buffers_;

    std::atomic<bool> running_{false};
    std::thread thread_;
    ViewportInfo info_;

#ifdef TOI_ENABLE_OVRTX
    [[nodiscard]] bool ensure_growth_renderer();
    // Records the ovrtx growth frame into the swapchain image; returns false to
    // fall back to the test pattern (renderer not ready or a frame failed).
    [[nodiscard]] bool record_growth_frame(VkCommandBuffer command_buffer, std::uint32_t image_index);

    std::mutex stage_mutex_;
    std::optional<render::GrowthPreviewStageProjection> pending_stage_;
    bool stage_dirty_ = false;

    std::unique_ptr<ovrtx::RendererSession> renderer_;
    CudaInteropImage interop_;
    CudaInteropSemaphore cuda_done_;
    ViewportOverlay overlay_;
    bool growth_ready_ = false;
    bool growth_failed_ = false;
    std::atomic<bool> guides_visible_{true};
    std::atomic<bool> world_origin_axes_visible_{true};

    void poll_pointer();

    std::mutex camera_mutex_;
    render::OrbitView orbit_;
    render::GrowthPreviewCamera base_camera_;
    bool orbit_initialized_ = false;
    bool orbit_dirty_ = false;
    bool has_base_camera_ = false;
    int last_pointer_x_ = 0;
    int last_pointer_y_ = 0;
    bool last_dragging_ = false;
#endif
};

} // namespace toi::viewport
