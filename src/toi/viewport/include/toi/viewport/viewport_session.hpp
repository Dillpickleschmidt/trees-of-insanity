#pragma once

#include "toi/ovrtx/renderer_session.hpp"
#include "toi/render/orbit_view.hpp"
#include "toi/viewport/cuda_vulkan_interop.hpp"
#include "toi/viewport/error.hpp"
#include "toi/viewport/viewport_overlay.hpp"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace toi::viewport {

class VulkanPresenter;

struct ViewportExtent {
    int width = 0;
    int height = 0;
};

struct ViewportInfo {
    std::string device_name;
    ViewportExtent extent;
};

enum class ViewportPhase {
    Starting,
    Warming,
    Rendering,
    Ready,
    Resizing,
    Error,
};

struct ViewportStatus {
    ViewportPhase phase = ViewportPhase::Starting;
    std::string message;
    ViewportExtent swapchain;
    ViewportExtent color;
    std::optional<ViewportExtent> depth;
    std::uint64_t frame_generation = 0;
};

enum class ViewportCameraFraming {
    PreserveOrbit,
    AutoFrame,
};

enum class ViewportCameraInputKind {
    Orbit,
    Pan,
    Dolly,
    Wheel,
};

struct ViewportCameraInput {
    ViewportCameraInputKind kind = ViewportCameraInputKind::Orbit;
    float dx = 0.0F;
    float dy = 0.0F;
    int viewport_height = 1;
};

// Owns one ovrtx Growth preview render loop and its Vulkan presentation module.
class ViewportSession {
public:
    [[nodiscard]] static Result<std::unique_ptr<ViewportSession>> attach(std::uintptr_t native_window, int width,
                                                                         int height);

    ViewportSession(const ViewportSession&) = delete;
    ViewportSession& operator=(const ViewportSession&) = delete;
    ~ViewportSession();

    [[nodiscard]] const ViewportInfo& info() const;
    [[nodiscard]] ViewportStatus status() const;
    void report_error(std::string message);
    void set_pending_stage(render::GrowthPreviewStageProjection stage,
                           ViewportCameraFraming framing = ViewportCameraFraming::PreserveOrbit);
    void set_guide_options(bool guides_visible, bool world_origin_axes_visible);
    [[nodiscard]] bool apply_camera_input(ViewportCameraInput input);
    void surface_changed();

private:
    struct GrowthSlot;

    ViewportSession() = default;

    [[nodiscard]] Result<void> recreate_swapchain();
    void render_loop();
    void set_status_phase(ViewportPhase phase, std::string message = {});
    void set_status_error(std::string message);
    void update_swapchain_status();
    void mark_frame_presented(const GrowthSlot& slot);

    [[nodiscard]] bool ensure_growth_renderer();
    [[nodiscard]] Result<void> resize_growth_slot(int slot_index, int width, int height);
    [[nodiscard]] bool produce_growth_frame();
    void complete_pending_produce();
    void record_growth_present(VkCommandBuffer command_buffer, std::uint32_t image_index);

    struct GrowthSlot {
        CudaInteropImage color;
        CudaInteropFloatImage distance;
        render::GrowthPreviewCamera camera{};
        std::uint64_t timeline_value = 0;
        bool has_distance = false;
    };

    std::unique_ptr<VulkanPresenter> presenter_;
    std::atomic<bool> running_{false};
    std::thread thread_;
    ViewportInfo info_;
    mutable std::mutex status_mutex_;
    ViewportStatus status_;

    std::mutex stage_mutex_;
    std::optional<render::GrowthPreviewStageProjection> pending_stage_;
    ViewportCameraFraming pending_stage_framing_ = ViewportCameraFraming::AutoFrame;
    bool stage_dirty_ = false;

    std::unique_ptr<ovrtx::RendererSession> renderer_;
    GrowthSlot slots_[2];
    int present_slot_ = 0;
    int produce_slot_ = 1;
    CudaInteropTimelineSemaphore frames_ready_;
    std::uint64_t timeline_value_ = 0;
    cudaEvent_t copy_done_ = nullptr;
    bool produce_pending_ = false;
    ViewportOverlay overlay_;
    bool growth_ready_ = false;
    bool growth_failed_ = false;
    bool last_draw_guides_ = false;
    std::atomic<bool> needs_present_{true};
    std::atomic<bool> guides_visible_{true};
    std::atomic<bool> world_origin_axes_visible_{true};

    std::mutex camera_mutex_;
    std::optional<render::OrbitView> orbit_;
    std::optional<render::GrowthPreviewCamera> base_camera_;
    bool orbit_dirty_ = false;
};

} // namespace toi::viewport
