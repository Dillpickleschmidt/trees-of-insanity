#include "toi/viewport/vulkan_context.hpp"

#include <utility>

namespace toi::viewport {

VulkanContext VulkanContext::borrow(VkPhysicalDevice physical_device, VkDevice device, VkQueue graphics_queue,
                                    std::uint32_t queue_family, VkCommandPool command_pool, int cuda_device,
                                    std::string device_name)
{
    VulkanContext context;
    context.physical_device_ = physical_device;
    context.device_ = device;
    context.graphics_queue_ = graphics_queue;
    context.queue_family_ = queue_family;
    context.command_pool_ = command_pool;
    context.info_ = {
        .physical_device_name = std::move(device_name),
        .cuda_device = cuda_device,
    };
    return context;
}

VulkanContext::VulkanContext(VulkanContext&& other) noexcept
    : physical_device_(std::exchange(other.physical_device_, VK_NULL_HANDLE))
    , device_(std::exchange(other.device_, VK_NULL_HANDLE))
    , graphics_queue_(std::exchange(other.graphics_queue_, VK_NULL_HANDLE))
    , queue_family_(std::exchange(other.queue_family_, 0))
    , command_pool_(std::exchange(other.command_pool_, VK_NULL_HANDLE))
    , info_(std::move(other.info_))
{
}

VulkanContext& VulkanContext::operator=(VulkanContext&& other) noexcept
{
    if (this != &other) {
        physical_device_ = std::exchange(other.physical_device_, VK_NULL_HANDLE);
        device_ = std::exchange(other.device_, VK_NULL_HANDLE);
        graphics_queue_ = std::exchange(other.graphics_queue_, VK_NULL_HANDLE);
        queue_family_ = std::exchange(other.queue_family_, 0);
        command_pool_ = std::exchange(other.command_pool_, VK_NULL_HANDLE);
        info_ = std::move(other.info_);
    }
    return *this;
}

VkPhysicalDevice VulkanContext::physical_device() const { return physical_device_; }
VkDevice VulkanContext::device() const { return device_; }
VkQueue VulkanContext::graphics_queue() const { return graphics_queue_; }
std::uint32_t VulkanContext::queue_family() const { return queue_family_; }
VkCommandPool VulkanContext::command_pool() const { return command_pool_; }
const VulkanContextInfo& VulkanContext::info() const { return info_; }

} // namespace toi::viewport
