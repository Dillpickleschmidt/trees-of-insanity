#pragma once

#include <vulkan/vulkan.h>

#include <QVulkanInstance>

#include <cstdint>
#include <memory>
#include <string>

class QQuickWindow;

class VulkanDevice final {
public:
    static std::unique_ptr<VulkanDevice> create(QQuickWindow& window, QVulkanInstance& instance, int cudaDevice);

    VulkanDevice(const VulkanDevice&) = delete;
    VulkanDevice& operator=(const VulkanDevice&) = delete;
    ~VulkanDevice();

    VkPhysicalDevice physicalDevice() const;
    VkDevice device() const;
    VkQueue queue() const;
    std::uint32_t queueFamily() const;
    const std::string& name() const;

private:
    VulkanDevice() = default;

    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    std::uint32_t queueFamily_ = 0;
    std::string name_;
};
