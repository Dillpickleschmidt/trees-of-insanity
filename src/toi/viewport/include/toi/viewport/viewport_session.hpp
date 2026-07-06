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

namespace toi::viewport {

struct ViewportInfo {
    std::string device_name;
    int width = 0;
    int height = 0;
};

// Owns a Vulkan presentation loop targeting the shell's native X11 window.
// For now it presents an animated clear color (a viewport test pattern),
// proving the surface/swapchain/present path end to end; the ovrtx CUDA frame
// source replaces the clear step later.
class ViewportSession {
public:
    [[nodiscard]] static Result<std::unique_ptr<ViewportSession>> attach(unsigned long x_window, int width,
                                                                         int height);

    ViewportSession(const ViewportSession&) = delete;
    ViewportSession& operator=(const ViewportSession&) = delete;
    ~ViewportSession();

    [[nodiscard]] const ViewportInfo& info() const;

private:
    static constexpr int kFramesInFlight = 2;

    ViewportSession() = default;

    [[nodiscard]] Result<void> create_sync_objects();
    [[nodiscard]] Result<void> create_swapchain_resources();
    void destroy_swapchain_resources();
    [[nodiscard]] Result<void> recreate_swapchain();
    void render_loop();

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
};

} // namespace toi::viewport
