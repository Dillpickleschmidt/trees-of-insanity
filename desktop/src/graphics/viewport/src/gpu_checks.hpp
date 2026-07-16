#pragma once

#include "toi/viewport/error.hpp"

#include <cuda_runtime_api.h>
#include <vulkan/vulkan.h>

#include <cstdint>
#include <string>
#include <string_view>

namespace toi::viewport {

[[nodiscard]] std::string vk_error(std::string_view context, VkResult result);
[[nodiscard]] std::string cuda_error(std::string_view context, cudaError_t result);
[[nodiscard]] Result<void> require_vk_success(VkResult result, std::string_view context);
[[nodiscard]] Result<void> require_cuda_success(cudaError_t result, std::string_view context);

// ovrtx stream handles encode "no stream" as 0/1; anything else is a cudaStream_t.
[[nodiscard]] cudaStream_t cuda_stream_from_ovrtx(std::uintptr_t stream);

[[nodiscard]] Result<std::uint32_t> find_memory_type(VkPhysicalDevice physical_device, std::uint32_t type_filter,
                                                     VkMemoryPropertyFlags properties);

} // namespace toi::viewport
