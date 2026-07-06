#pragma once

#include "toi/viewport/error.hpp"
#include "toi/viewport/x11_vulkan_surface.hpp"

#ifndef VK_USE_PLATFORM_XLIB_KHR
#define VK_USE_PLATFORM_XLIB_KHR
#endif
#include <vulkan/vulkan.h>

#include <cstdint>
#include <string>

namespace toi::viewport {

struct VulkanContextInfo {
    std::string physical_device_name;
    std::uint32_t vendor_id = 0;
    std::uint32_t device_id = 0;
};

// Vulkan instance + X11 surface + presentation-capable device. Kept to the
// surface/present essentials; CUDA/Vulkan interop extensions are layered on in
// the growth-preview work, not here.
class VulkanContext {
public:
    [[nodiscard]] static Result<VulkanContext> create(const NativeSurfaceHandle& surface_handle);

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
