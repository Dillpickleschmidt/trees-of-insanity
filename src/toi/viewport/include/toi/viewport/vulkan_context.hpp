#pragma once

#include "toi/viewport/error.hpp"
#include "toi/viewport/native_surface.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <string>

namespace toi::viewport {

struct VulkanContextInfo {
    std::string physical_device_name;
    std::uint32_t vendor_id = 0;
    std::uint32_t device_id = 0;
    int cuda_device = -1;
};

// Vulkan instance + native surface + presentation-capable device. ovrtx builds
// require the Vulkan physical device to match the selected CUDA device.
class VulkanContext {
public:
    [[nodiscard]] static Result<VulkanContext> create(const NativeSurface& surface, int cuda_device);

    VulkanContext() = default;
    VulkanContext(const VulkanContext&) = delete;
    VulkanContext& operator=(const VulkanContext&) = delete;
    VulkanContext(VulkanContext&& other) noexcept;
    VulkanContext& operator=(VulkanContext&& other) noexcept;
    ~VulkanContext();

    [[nodiscard]] VkSurfaceKHR surface() const;
    [[nodiscard]] VkPhysicalDevice physical_device() const;
    [[nodiscard]] VkDevice device() const;
    [[nodiscard]] VkQueue graphics_queue() const;
    [[nodiscard]] std::uint32_t queue_family() const;
    [[nodiscard]] VkCommandPool command_pool() const;
    [[nodiscard]] const VulkanContextInfo& info() const;

private:
    void reset();

    VkInstance instance_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice physical_device_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue graphics_queue_ = VK_NULL_HANDLE;
    std::uint32_t queue_family_ = 0;
    VkCommandPool command_pool_ = VK_NULL_HANDLE;
    VulkanContextInfo info_;
};

} // namespace toi::viewport
