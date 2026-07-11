#pragma once

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

// Non-owning Vulkan device view used by the graphics module. The Qt shell owns
// instance, device, queue, command pool, and their destruction order.
class VulkanContext {
public:
    [[nodiscard]] static VulkanContext borrow(VkPhysicalDevice physical_device, VkDevice device, VkQueue graphics_queue,
                                               std::uint32_t queue_family, VkCommandPool command_pool,
                                               int cuda_device, std::string device_name);

    VulkanContext() = default;
    VulkanContext(const VulkanContext&) = delete;
    VulkanContext& operator=(const VulkanContext&) = delete;
    VulkanContext(VulkanContext&& other) noexcept;
    VulkanContext& operator=(VulkanContext&& other) noexcept;
    ~VulkanContext() = default;

    [[nodiscard]] VkPhysicalDevice physical_device() const;
    [[nodiscard]] VkDevice device() const;
    [[nodiscard]] VkQueue graphics_queue() const;
    [[nodiscard]] std::uint32_t queue_family() const;
    [[nodiscard]] VkCommandPool command_pool() const;
    [[nodiscard]] const VulkanContextInfo& info() const;

private:
    VkPhysicalDevice physical_device_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue graphics_queue_ = VK_NULL_HANDLE;
    std::uint32_t queue_family_ = 0;
    VkCommandPool command_pool_ = VK_NULL_HANDLE;
    VulkanContextInfo info_;
};

} // namespace toi::viewport
