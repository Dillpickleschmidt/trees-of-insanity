#pragma once

#include "toi/viewport/error.hpp"
#include "toi/viewport/vulkan_context.hpp"

#include <vulkan/vulkan.h>

#include <vector>

namespace toi::viewport {

class VulkanSwapchain {
public:
    [[nodiscard]] static Result<VulkanSwapchain> create(VulkanContext& context, int width, int height);

    VulkanSwapchain() = default;
    VulkanSwapchain(const VulkanSwapchain&) = delete;
    VulkanSwapchain& operator=(const VulkanSwapchain&) = delete;
    VulkanSwapchain(VulkanSwapchain&& other) noexcept;
    VulkanSwapchain& operator=(VulkanSwapchain&& other) noexcept;
    ~VulkanSwapchain();

    [[nodiscard]] VkSwapchainKHR get() const;
    [[nodiscard]] VkExtent2D extent() const;
    [[nodiscard]] VkFormat format() const;
    [[nodiscard]] const std::vector<VkImage>& images() const;
    void reset();

private:
    VulkanContext* context_ = nullptr;
    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkExtent2D extent_{};
    VkFormat format_ = VK_FORMAT_UNDEFINED;
    std::vector<VkImage> images_;
};

} // namespace toi::viewport
