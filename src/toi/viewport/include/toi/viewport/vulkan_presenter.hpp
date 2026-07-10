#pragma once

#include "toi/viewport/error.hpp"
#include "toi/viewport/native_surface.hpp"
#include "toi/viewport/vulkan_context.hpp"
#include "toi/viewport/vulkan_swapchain.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace toi::viewport {

struct AcquiredViewportFrame {
    VkCommandBuffer command_buffer = VK_NULL_HANDLE;
    VkImage image = VK_NULL_HANDLE;
    std::uint32_t image_index = 0;
    bool swapchain_suboptimal = false;
};

struct ViewportTimelineWait {
    VkSemaphore semaphore = VK_NULL_HANDLE;
    std::uint64_t value = 0;
    VkPipelineStageFlags stages = 0;
};

// Owns the native surface, Vulkan context, swapchain, and presentation
// synchronization for one Electrobun viewport.
class VulkanPresenter {
public:
    [[nodiscard]] static Result<std::unique_ptr<VulkanPresenter>> attach(std::uintptr_t native_window, int width,
                                                                         int height, int cuda_device);

    VulkanPresenter(const VulkanPresenter&) = delete;
    VulkanPresenter& operator=(const VulkanPresenter&) = delete;
    ~VulkanPresenter();

    [[nodiscard]] Result<void> wait_for_frame_slot();
    [[nodiscard]] Result<std::optional<AcquiredViewportFrame>> acquire_frame();
    // Returns true when presentation succeeded but the swapchain should be recreated.
    [[nodiscard]] Result<bool> submit_and_present(const AcquiredViewportFrame& frame, ViewportTimelineWait timeline_wait);
    [[nodiscard]] Result<void> recreate_swapchain();
    [[nodiscard]] Result<void> wait_idle();

    [[nodiscard]] VulkanContext& context();
    [[nodiscard]] VulkanSwapchain& swapchain();
    [[nodiscard]] const VulkanSwapchain& swapchain() const;
    [[nodiscard]] const VulkanContextInfo& context_info() const;

private:
    VulkanPresenter() = default;

    [[nodiscard]] Result<void> create_frame_resources();
    void destroy_frame_resources();

    NativeSurface surface_;
    VulkanContext context_;
    VulkanSwapchain swapchain_;

    VkSemaphore image_available_ = VK_NULL_HANDLE;
    VkFence in_flight_ = VK_NULL_HANDLE;
    std::vector<VkSemaphore> render_finished_;
    std::vector<VkCommandBuffer> command_buffers_;
};

} // namespace toi::viewport
