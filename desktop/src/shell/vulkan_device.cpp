#include "vulkan_device.hpp"

#include <QQuickGraphicsConfiguration>
#include <QQuickGraphicsDevice>
#include <QQuickWindow>

#include <cuda_runtime_api.h>
#include <rhi/qrhi.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

std::array<unsigned char, VK_UUID_SIZE> cudaUuid(int cudaDevice)
{
    cudaDeviceProp properties{};
    const auto result = cudaGetDeviceProperties(&properties, cudaDevice);
    if (result != cudaSuccess) {
        throw std::runtime_error(std::string("cudaGetDeviceProperties failed: ") + cudaGetErrorString(result));
    }
    std::array<unsigned char, VK_UUID_SIZE> uuid{};
    std::memcpy(uuid.data(), properties.uuid.bytes, uuid.size());
    return uuid;
}

std::vector<VkExtensionProperties> deviceExtensions(VkPhysicalDevice device)
{
    std::uint32_t count = 0;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> extensions(count);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &count, extensions.data());
    return extensions;
}

bool hasExtension(const std::vector<VkExtensionProperties>& extensions, const std::string& name)
{
    return std::ranges::any_of(extensions, [&](const auto& extension) { return name == extension.extensionName; });
}

std::uint32_t chooseQueueFamily(VkPhysicalDevice device, VkSurfaceKHR surface)
{
    std::uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, nullptr);
    std::vector<VkQueueFamilyProperties> properties(count);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, properties.data());
    for (std::uint32_t index = 0; index < count; ++index) {
        VkBool32 present = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, index, surface, &present);
        if ((properties[index].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0 && present == VK_TRUE) {
            return index;
        }
    }
    throw std::runtime_error("CUDA-matching Vulkan device has no graphics/present queue");
}

VkPhysicalDevice choosePhysicalDevice(VkInstance instance, int cudaDevice)
{
    std::uint32_t count = 0;
    if (vkEnumeratePhysicalDevices(instance, &count, nullptr) != VK_SUCCESS || count == 0) {
        throw std::runtime_error("no Vulkan physical devices available");
    }
    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(instance, &count, devices.data());
    const auto expectedUuid = cudaUuid(cudaDevice);
    for (VkPhysicalDevice device : devices) {
        VkPhysicalDeviceIDProperties id{};
        id.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES;
        VkPhysicalDeviceProperties2 properties{};
        properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        properties.pNext = &id;
        vkGetPhysicalDeviceProperties2(device, &properties);
        if (std::equal(expectedUuid.begin(), expectedUuid.end(), std::begin(id.deviceUUID))) {
            return device;
        }
    }
    throw std::runtime_error("no Vulkan physical device matches CUDA device " + std::to_string(cudaDevice));
}

std::vector<std::string> requiredDeviceExtensions()
{
    std::set<std::string> names;
    for (const QByteArray& extension : QRhiVulkanInitParams::preferredExtensionsForImportedDevice()) {
        names.insert(extension.toStdString());
    }
    names.insert(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    names.insert(VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME);
    names.insert(VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME);
#if defined(Q_OS_WIN)
    names.insert(VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME);
    names.insert(VK_KHR_EXTERNAL_SEMAPHORE_WIN32_EXTENSION_NAME);
#else
    names.insert(VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME);
    names.insert(VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME);
#endif
    names.insert(VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME);
    names.insert(VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME);
    return {names.begin(), names.end()};
}

} // namespace

std::unique_ptr<VulkanDevice> VulkanDevice::create(QQuickWindow& window, QVulkanInstance& instance, int cudaDevice)
{
    window.setVulkanInstance(&instance);
    window.create();
    const VkSurfaceKHR surface = instance.surfaceForWindow(&window);
    if (surface == VK_NULL_HANDLE) {
        throw std::runtime_error("Qt failed to create a Vulkan window surface");
    }

    const VkPhysicalDevice physicalDevice = choosePhysicalDevice(instance.vkInstance(), cudaDevice);
    const std::uint32_t queueFamily = chooseQueueFamily(physicalDevice, surface);
    const auto available = deviceExtensions(physicalDevice);
    const auto required = requiredDeviceExtensions();
    std::vector<const char*> extensionNames;
    extensionNames.reserve(required.size());
    for (const std::string& extension : required) {
        if (!hasExtension(available, extension)) {
            throw std::runtime_error("missing Vulkan device extension " + extension);
        }
        extensionNames.push_back(extension.c_str());
    }

    const float priority = 1.0F;
    VkDeviceQueueCreateInfo queueInfo{};
    queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueInfo.queueFamilyIndex = queueFamily;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &priority;

    VkPhysicalDeviceVulkan12Features features12{};
    features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    features12.timelineSemaphore = VK_TRUE;

    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.pNext = &features12;
    createInfo.queueCreateInfoCount = 1;
    createInfo.pQueueCreateInfos = &queueInfo;
    createInfo.enabledExtensionCount = static_cast<std::uint32_t>(extensionNames.size());
    createInfo.ppEnabledExtensionNames = extensionNames.data();

    VkDevice device = VK_NULL_HANDLE;
    const VkResult result = vkCreateDevice(physicalDevice, &createInfo, nullptr, &device);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("vkCreateDevice failed: " + std::to_string(static_cast<int>(result)));
    }

    VkQueue queue = VK_NULL_HANDLE;
    vkGetDeviceQueue(device, queueFamily, 0, &queue);
    window.setGraphicsDevice(QQuickGraphicsDevice::fromDeviceObjects(physicalDevice, device, static_cast<int>(queueFamily)));

    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(physicalDevice, &properties);
    auto context = std::unique_ptr<VulkanDevice>(new VulkanDevice);
    context->physicalDevice_ = physicalDevice;
    context->device_ = device;
    context->queue_ = queue;
    context->queueFamily_ = queueFamily;
    context->name_ = properties.deviceName;
    return context;
}

VulkanDevice::~VulkanDevice()
{
    if (device_ != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device_);
        vkDestroyDevice(device_, nullptr);
    }
}

VkPhysicalDevice VulkanDevice::physicalDevice() const { return physicalDevice_; }
VkDevice VulkanDevice::device() const { return device_; }
VkQueue VulkanDevice::queue() const { return queue_; }
std::uint32_t VulkanDevice::queueFamily() const { return queueFamily_; }
const std::string& VulkanDevice::name() const { return name_; }
