#include "gpu_checks.hpp"

#include <sstream>

namespace toi::viewport {

std::string vk_error(std::string_view context, VkResult result)
{
    std::ostringstream out;
    out << context << " failed: VkResult " << static_cast<int>(result);
    return out.str();
}

std::string cuda_error(std::string_view context, cudaError_t result)
{
    std::ostringstream out;
    out << context << " failed: " << cudaGetErrorString(result);
    return out.str();
}

Result<void> require_vk_success(VkResult result, std::string_view context)
{
    if (result != VK_SUCCESS) {
        return std::unexpected(make_error(vk_error(context, result)));
    }
    return {};
}

Result<void> require_cuda_success(cudaError_t result, std::string_view context)
{
    if (result != cudaSuccess) {
        return std::unexpected(make_error(cuda_error(context, result)));
    }
    return {};
}

cudaStream_t cuda_stream_from_ovrtx(std::uintptr_t stream)
{
    if (stream <= std::uintptr_t{1}) {
        return nullptr;
    }
    return reinterpret_cast<cudaStream_t>(stream);
}

Result<std::uint32_t> find_memory_type(VkPhysicalDevice physical_device, std::uint32_t type_filter,
                                       VkMemoryPropertyFlags properties)
{
    VkPhysicalDeviceMemoryProperties memory_properties{};
    vkGetPhysicalDeviceMemoryProperties(physical_device, &memory_properties);
    for (std::uint32_t index = 0; index < memory_properties.memoryTypeCount; ++index) {
        if ((type_filter & (std::uint32_t{1} << index)) != 0 &&
            (memory_properties.memoryTypes[index].propertyFlags & properties) == properties) {
            return index;
        }
    }
    return std::unexpected(make_error("no Vulkan memory type matches the requested properties"));
}

} // namespace toi::viewport
