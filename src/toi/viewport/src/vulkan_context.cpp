#include "toi/viewport/vulkan_context.hpp"

#include <cuda_runtime_api.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace toi::viewport {
namespace {

[[nodiscard]] std::string vk_error(std::string_view context, VkResult result)
{
    std::ostringstream out;
    out << context << " failed: VkResult " << static_cast<int>(result);
    return out.str();
}

[[nodiscard]] bool has_extension(const std::vector<VkExtensionProperties>& extensions, const char* name)
{
    return std::ranges::any_of(
        extensions, [name](const auto& extension) { return std::strcmp(extension.extensionName, name) == 0; });
}

[[nodiscard]] std::vector<VkExtensionProperties> instance_extensions()
{
    std::uint32_t count = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> extensions(count);
    vkEnumerateInstanceExtensionProperties(nullptr, &count, extensions.data());
    return extensions;
}

[[nodiscard]] std::vector<VkExtensionProperties> device_extensions(VkPhysicalDevice physical_device)
{
    std::uint32_t count = 0;
    vkEnumerateDeviceExtensionProperties(physical_device, nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> extensions(count);
    vkEnumerateDeviceExtensionProperties(physical_device, nullptr, &count, extensions.data());
    return extensions;
}

[[nodiscard]] Result<VkInstance> create_instance()
{
    const auto available_extensions = instance_extensions();
    std::vector<const char*> required_extensions = {
        VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME,
        VK_KHR_EXTERNAL_SEMAPHORE_CAPABILITIES_EXTENSION_NAME,
        VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME,
    };
    const auto surface_extensions = NativeSurface::required_vulkan_instance_extensions();
    required_extensions.insert(required_extensions.end(), surface_extensions.begin(), surface_extensions.end());
    for (const char* extension : required_extensions) {
        if (!has_extension(available_extensions, extension)) {
            return std::unexpected(make_error(std::string("missing Vulkan instance extension ") + extension));
        }
    }

    VkApplicationInfo app_info{};
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = "Trees of Insanity";
    app_info.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    app_info.pEngineName = "Trees of Insanity";
    app_info.engineVersion = VK_MAKE_VERSION(0, 1, 0);
    app_info.apiVersion = VK_API_VERSION_1_2;

    VkInstanceCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    create_info.pApplicationInfo = &app_info;
    create_info.enabledExtensionCount = static_cast<std::uint32_t>(required_extensions.size());
    create_info.ppEnabledExtensionNames = required_extensions.data();

    VkInstance instance = VK_NULL_HANDLE;
    const auto result = vkCreateInstance(&create_info, nullptr, &instance);
    if (result != VK_SUCCESS) {
        return std::unexpected(make_error(vk_error("vkCreateInstance", result)));
    }
    return instance;
}

[[nodiscard]] Result<std::array<unsigned char, VK_UUID_SIZE>> cuda_device_uuid(int cuda_device)
{
    cudaDeviceProp properties{};
    if (const auto result = cudaGetDeviceProperties(&properties, cuda_device); result != cudaSuccess) {
        return std::unexpected(make_error(std::string("cudaGetDeviceProperties failed: ") + cudaGetErrorString(result)));
    }
    static_assert(sizeof(properties.uuid.bytes) == VK_UUID_SIZE);
    std::array<unsigned char, VK_UUID_SIZE> uuid{};
    std::memcpy(uuid.data(), properties.uuid.bytes, uuid.size());
    return uuid;
}

[[nodiscard]] bool supports_external_image(VkPhysicalDevice physical_device, VkFormat format,
                                           VkImageUsageFlags usage)
{
    VkPhysicalDeviceExternalImageFormatInfo external_info{};
    external_info.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO;
    external_info.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
    VkPhysicalDeviceImageFormatInfo2 image_info{};
    image_info.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2;
    image_info.pNext = &external_info;
    image_info.format = format;
    image_info.type = VK_IMAGE_TYPE_2D;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.usage = usage;
    image_info.flags = 0;

    VkExternalImageFormatProperties external_properties{};
    external_properties.sType = VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES;
    VkImageFormatProperties2 properties{};
    properties.sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2;
    properties.pNext = &external_properties;
    if (vkGetPhysicalDeviceImageFormatProperties2(physical_device, &image_info, &properties) != VK_SUCCESS) {
        return false;
    }
    return (external_properties.externalMemoryProperties.externalMemoryFeatures &
            VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT) != 0;
}

[[nodiscard]] bool supports_external_timeline_semaphore(VkPhysicalDevice physical_device)
{
    VkPhysicalDeviceExternalSemaphoreInfo info{};
    info.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_SEMAPHORE_INFO;
    info.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT;
    VkExternalSemaphoreProperties properties{};
    properties.sType = VK_STRUCTURE_TYPE_EXTERNAL_SEMAPHORE_PROPERTIES;
    vkGetPhysicalDeviceExternalSemaphoreProperties(physical_device, &info, &properties);
    return (properties.externalSemaphoreFeatures & VK_EXTERNAL_SEMAPHORE_FEATURE_EXPORTABLE_BIT) != 0;
}

[[nodiscard]] bool supports_cuda_interop(VkPhysicalDevice physical_device)
{
    const auto extensions = device_extensions(physical_device);
    const std::array required_extensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME,
        VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME,
        VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME,
        VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME,
        VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME,
        VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME,
    };
    if (!std::ranges::all_of(required_extensions,
                             [&extensions](const char* name) { return has_extension(extensions, name); })) {
        return false;
    }

    VkPhysicalDeviceVulkan12Features features12{};
    features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    VkPhysicalDeviceFeatures2 features{};
    features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    features.pNext = &features12;
    vkGetPhysicalDeviceFeatures2(physical_device, &features);
    return features12.timelineSemaphore == VK_TRUE &&
           supports_external_image(physical_device, VK_FORMAT_R8G8B8A8_UNORM,
                                   VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT) &&
           supports_external_image(physical_device, VK_FORMAT_R32_SFLOAT, VK_IMAGE_USAGE_SAMPLED_BIT) &&
           supports_external_timeline_semaphore(physical_device);
}

[[nodiscard]] std::optional<std::uint32_t> graphics_present_queue_family(VkPhysicalDevice physical_device,
                                                                         VkSurfaceKHR surface)
{
    std::uint32_t queue_family_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &queue_family_count, nullptr);
    std::vector<VkQueueFamilyProperties> queue_families(queue_family_count);
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &queue_family_count, queue_families.data());

    for (std::uint32_t index = 0; index < queue_family_count; ++index) {
        VkBool32 present_supported = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(physical_device, index, surface, &present_supported);
        if ((queue_families[index].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0 && present_supported == VK_TRUE) {
            return index;
        }
    }
    return std::nullopt;
}

struct PhysicalDeviceChoice {
    VkPhysicalDevice physical_device = VK_NULL_HANDLE;
    std::uint32_t queue_family = 0;
    VulkanContextInfo info;
};

[[nodiscard]] Result<PhysicalDeviceChoice> choose_physical_device(VkInstance instance, VkSurfaceKHR surface,
                                                                  int cuda_device)
{
    std::uint32_t device_count = 0;
    auto result = vkEnumeratePhysicalDevices(instance, &device_count, nullptr);
    if (result != VK_SUCCESS || device_count == 0) {
        return std::unexpected(make_error(vk_error("vkEnumeratePhysicalDevices", result)));
    }
    std::vector<VkPhysicalDevice> physical_devices(device_count);
    if (const auto enumerated = vkEnumeratePhysicalDevices(instance, &device_count, physical_devices.data());
        enumerated != VK_SUCCESS) {
        return std::unexpected(make_error(vk_error("vkEnumeratePhysicalDevices", enumerated)));
    }

    auto cuda_uuid = cuda_device_uuid(cuda_device);
    if (!cuda_uuid) {
        return std::unexpected(cuda_uuid.error());
    }

    PhysicalDeviceChoice best;
    for (VkPhysicalDevice physical_device : physical_devices) {
        if (!supports_cuda_interop(physical_device)) {
            continue;
        }
        auto queue_family = graphics_present_queue_family(physical_device, surface);
        if (!queue_family) {
            continue;
        }

        VkPhysicalDeviceIDProperties id{};
        id.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES;
        VkPhysicalDeviceProperties2 properties2{};
        properties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        properties2.pNext = &id;
        vkGetPhysicalDeviceProperties2(physical_device, &properties2);
        if (!std::equal(cuda_uuid->begin(), cuda_uuid->end(), std::begin(id.deviceUUID))) {
            continue;
        }
        const auto& properties = properties2.properties;
        best = PhysicalDeviceChoice{
            .physical_device = physical_device,
            .queue_family = *queue_family,
            .info =
                VulkanContextInfo{
                    .physical_device_name = properties.deviceName,
                    .vendor_id = properties.vendorID,
                    .device_id = properties.deviceID,
                    .cuda_device = cuda_device,
                },
        };
        break;
    }

    if (best.physical_device == VK_NULL_HANDLE) {
        return std::unexpected(
            make_error("no presentation-capable Vulkan device matches CUDA device " + std::to_string(cuda_device)));
    }
    return best;
}

[[nodiscard]] Result<VkDevice> create_device(VkPhysicalDevice physical_device, std::uint32_t queue_family)
{
    const float priority = 1.0F;
    VkDeviceQueueCreateInfo queue_info{};
    queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_info.queueFamilyIndex = queue_family;
    queue_info.queueCount = 1;
    queue_info.pQueuePriorities = &priority;

    const std::array device_extensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME,
        VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME,
        VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME,
        VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME,
        VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME,
        VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME,
    };

    VkPhysicalDeviceFeatures features{};
    VkDeviceCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    create_info.queueCreateInfoCount = 1;
    create_info.pQueueCreateInfos = &queue_info;
    create_info.enabledExtensionCount = static_cast<std::uint32_t>(device_extensions.size());
    create_info.ppEnabledExtensionNames = device_extensions.data();
    create_info.pEnabledFeatures = &features;

    VkPhysicalDeviceVulkan12Features features12{};
    features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    features12.timelineSemaphore = VK_TRUE;
    create_info.pNext = &features12;

    VkDevice device = VK_NULL_HANDLE;
    const auto result = vkCreateDevice(physical_device, &create_info, nullptr, &device);
    if (result != VK_SUCCESS) {
        return std::unexpected(make_error(vk_error("vkCreateDevice", result)));
    }
    return device;
}

[[nodiscard]] Result<VkCommandPool> create_command_pool(VkDevice device, std::uint32_t queue_family)
{
    VkCommandPoolCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    create_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    create_info.queueFamilyIndex = queue_family;

    VkCommandPool command_pool = VK_NULL_HANDLE;
    const auto result = vkCreateCommandPool(device, &create_info, nullptr, &command_pool);
    if (result != VK_SUCCESS) {
        return std::unexpected(make_error(vk_error("vkCreateCommandPool", result)));
    }
    return command_pool;
}

} // namespace

Result<VulkanContext> VulkanContext::create(const NativeSurface& surface_handle, int cuda_device)
{
    auto instance = create_instance();
    if (!instance) {
        return std::unexpected(instance.error());
    }

    auto surface = surface_handle.create_vulkan_surface(*instance);
    if (!surface) {
        vkDestroyInstance(*instance, nullptr);
        return std::unexpected(surface.error());
    }

    auto choice = choose_physical_device(*instance, *surface, cuda_device);
    if (!choice) {
        vkDestroySurfaceKHR(*instance, *surface, nullptr);
        vkDestroyInstance(*instance, nullptr);
        return std::unexpected(choice.error());
    }

    auto device = create_device(choice->physical_device, choice->queue_family);
    if (!device) {
        vkDestroySurfaceKHR(*instance, *surface, nullptr);
        vkDestroyInstance(*instance, nullptr);
        return std::unexpected(device.error());
    }

    VkQueue queue = VK_NULL_HANDLE;
    vkGetDeviceQueue(*device, choice->queue_family, 0, &queue);

    auto command_pool = create_command_pool(*device, choice->queue_family);
    if (!command_pool) {
        vkDestroyDevice(*device, nullptr);
        vkDestroySurfaceKHR(*instance, *surface, nullptr);
        vkDestroyInstance(*instance, nullptr);
        return std::unexpected(command_pool.error());
    }

    VulkanContext context;
    context.instance_ = *instance;
    context.surface_ = *surface;
    context.physical_device_ = choice->physical_device;
    context.device_ = *device;
    context.graphics_queue_ = queue;
    context.queue_family_ = choice->queue_family;
    context.command_pool_ = *command_pool;
    context.info_ = std::move(choice->info);
    return context;
}

VulkanContext::VulkanContext(VulkanContext&& other) noexcept
    : instance_(std::exchange(other.instance_, VK_NULL_HANDLE))
    , surface_(std::exchange(other.surface_, VK_NULL_HANDLE))
    , physical_device_(std::exchange(other.physical_device_, VK_NULL_HANDLE))
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
        reset();
        instance_ = std::exchange(other.instance_, VK_NULL_HANDLE);
        surface_ = std::exchange(other.surface_, VK_NULL_HANDLE);
        physical_device_ = std::exchange(other.physical_device_, VK_NULL_HANDLE);
        device_ = std::exchange(other.device_, VK_NULL_HANDLE);
        graphics_queue_ = std::exchange(other.graphics_queue_, VK_NULL_HANDLE);
        queue_family_ = std::exchange(other.queue_family_, 0);
        command_pool_ = std::exchange(other.command_pool_, VK_NULL_HANDLE);
        info_ = std::move(other.info_);
    }
    return *this;
}

VulkanContext::~VulkanContext()
{
    reset();
}

VkSurfaceKHR VulkanContext::surface() const
{
    return surface_;
}
VkPhysicalDevice VulkanContext::physical_device() const
{
    return physical_device_;
}
VkDevice VulkanContext::device() const
{
    return device_;
}
VkQueue VulkanContext::graphics_queue() const
{
    return graphics_queue_;
}
std::uint32_t VulkanContext::queue_family() const
{
    return queue_family_;
}
VkCommandPool VulkanContext::command_pool() const
{
    return command_pool_;
}
const VulkanContextInfo& VulkanContext::info() const
{
    return info_;
}

void VulkanContext::reset()
{
    if (device_ != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device_);
        if (command_pool_ != VK_NULL_HANDLE) {
            vkDestroyCommandPool(device_, command_pool_, nullptr);
            command_pool_ = VK_NULL_HANDLE;
        }
        vkDestroyDevice(device_, nullptr);
        device_ = VK_NULL_HANDLE;
        graphics_queue_ = VK_NULL_HANDLE;
        physical_device_ = VK_NULL_HANDLE;
    }
    if (instance_ != VK_NULL_HANDLE && surface_ != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(instance_, surface_, nullptr);
        surface_ = VK_NULL_HANDLE;
    }
    if (instance_ != VK_NULL_HANDLE) {
        vkDestroyInstance(instance_, nullptr);
        instance_ = VK_NULL_HANDLE;
    }
    info_ = {};
}

} // namespace toi::viewport
