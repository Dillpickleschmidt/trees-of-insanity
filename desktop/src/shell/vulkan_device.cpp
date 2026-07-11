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

std::uint32_t deviceLocalMemoryType(VkPhysicalDevice physicalDevice, std::uint32_t typeBits)
{
    VkPhysicalDeviceMemoryProperties properties{};
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &properties);
    for (std::uint32_t index = 0; index < properties.memoryTypeCount; ++index) {
        if ((typeBits & (1U << index)) != 0 &&
            (properties.memoryTypes[index].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0) {
            return index;
        }
    }
    throw std::runtime_error("no device-local Vulkan memory type for preview image");
}

void createPreviewImage(VulkanDevice& context, VkImage& previewImage, VkDeviceMemory& previewMemory)
{
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageInfo.extent = {context.previewWidth(), context.previewHeight(), 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(context.device(), &imageInfo, nullptr, &previewImage) != VK_SUCCESS) {
        throw std::runtime_error("vkCreateImage failed for preview image");
    }

    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(context.device(), previewImage, &requirements);
    VkMemoryAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocateInfo.allocationSize = requirements.size;
    allocateInfo.memoryTypeIndex = deviceLocalMemoryType(context.physicalDevice(), requirements.memoryTypeBits);
    if (vkAllocateMemory(context.device(), &allocateInfo, nullptr, &previewMemory) != VK_SUCCESS ||
        vkBindImageMemory(context.device(), previewImage, previewMemory, 0) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate preview image memory");
    }

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = context.queueFamily();
    VkCommandPool pool = VK_NULL_HANDLE;
    vkCreateCommandPool(context.device(), &poolInfo, nullptr, &pool);
    VkCommandBufferAllocateInfo commandInfo{};
    commandInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    commandInfo.commandPool = pool;
    commandInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    commandInfo.commandBufferCount = 1;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(context.device(), &commandInfo, &commandBuffer);
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    VkImageMemoryBarrier toTransfer{};
    toTransfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toTransfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toTransfer.srcAccessMask = 0;
    toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toTransfer.image = previewImage;
    toTransfer.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                         0, nullptr, 0, nullptr, 1, &toTransfer);
    const VkClearColorValue color{{0.035F, 0.16F, 0.12F, 1.0F}};
    vkCmdClearColorImage(commandBuffer, previewImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &color, 1,
                         &toTransfer.subresourceRange);

    VkImageMemoryBarrier toSample = toTransfer;
    toSample.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toSample.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    toSample.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toSample.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                         0, nullptr, 0, nullptr, 1, &toSample);
    vkEndCommandBuffer(commandBuffer);
    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &commandBuffer;
    vkQueueSubmit(context.queue(), 1, &submit, VK_NULL_HANDLE);
    vkQueueWaitIdle(context.queue());
    vkDestroyCommandPool(context.device(), pool, nullptr);
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
    createPreviewImage(*context, context->previewImage_, context->previewMemory_);
    return context;
}

VulkanDevice::~VulkanDevice()
{
    if (device_ != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device_);
        if (previewImage_ != VK_NULL_HANDLE) {
            vkDestroyImage(device_, previewImage_, nullptr);
        }
        if (previewMemory_ != VK_NULL_HANDLE) {
            vkFreeMemory(device_, previewMemory_, nullptr);
        }
        vkDestroyDevice(device_, nullptr);
    }
}

VkPhysicalDevice VulkanDevice::physicalDevice() const { return physicalDevice_; }
VkDevice VulkanDevice::device() const { return device_; }
VkQueue VulkanDevice::queue() const { return queue_; }
std::uint32_t VulkanDevice::queueFamily() const { return queueFamily_; }
VkImage VulkanDevice::previewImage() const { return previewImage_; }
VkImageLayout VulkanDevice::previewImageLayout() const { return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; }
std::uint32_t VulkanDevice::previewWidth() const { return previewWidth_; }
std::uint32_t VulkanDevice::previewHeight() const { return previewHeight_; }
const std::string& VulkanDevice::name() const { return name_; }
