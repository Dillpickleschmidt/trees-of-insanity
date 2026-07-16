#pragma once

#include "toi/viewport/error.hpp"
#include "toi/viewport/vulkan_context.hpp"

#include <cuda_runtime_api.h>
#include <vulkan/vulkan.h>

#include <cstdint>

namespace toi::viewport {

[[nodiscard]] Result<void> select_cuda_device(int cuda_device);

// A Vulkan timeline semaphore shared with CUDA (opaque fd). CUDA signals value
// N after its copies for frame N; a Vulkan submit reading that frame waits for
// value >= N. Timeline waits are non-consuming, so re-presenting a cached frame
// re-waits the same value safely, and the monotonic counter orders frames
// across the double-buffered slots.
class CudaInteropTimelineSemaphore {
public:
    CudaInteropTimelineSemaphore() = default;
    CudaInteropTimelineSemaphore(const CudaInteropTimelineSemaphore&) = delete;
    CudaInteropTimelineSemaphore& operator=(const CudaInteropTimelineSemaphore&) = delete;
    CudaInteropTimelineSemaphore(CudaInteropTimelineSemaphore&& other) noexcept;
    CudaInteropTimelineSemaphore& operator=(CudaInteropTimelineSemaphore&& other) noexcept;
    ~CudaInteropTimelineSemaphore();

    [[nodiscard]] static Result<CudaInteropTimelineSemaphore> create(VulkanContext& context);
    [[nodiscard]] Result<void> signal_from_ovrtx_stream(std::uint64_t value, std::uintptr_t stream);
    [[nodiscard]] VkSemaphore vk_semaphore() const;

    void reset();

private:
    VkDevice device_ = VK_NULL_HANDLE;
    VkSemaphore vk_semaphore_ = VK_NULL_HANDLE;
    cudaExternalSemaphore_t cuda_semaphore_ = nullptr;
};

// A Vulkan image whose device memory is shared with a CUDA array via an
// opaque-fd external-memory handle. create_color makes the RGBA8 frame CUDA
// copies the ovrtx LdrColor array into and Vulkan blits to the viewport;
// create_distance makes the R32_SFLOAT image (with a view) the guide overlay
// samples for depth-aware occlusion (the ovrtx DistanceToCameraSD output).
class CudaInteropImage {
public:
    CudaInteropImage() = default;
    CudaInteropImage(const CudaInteropImage&) = delete;
    CudaInteropImage& operator=(const CudaInteropImage&) = delete;
    CudaInteropImage(CudaInteropImage&& other) noexcept;
    CudaInteropImage& operator=(CudaInteropImage&& other) noexcept;
    ~CudaInteropImage();

    [[nodiscard]] static Result<CudaInteropImage> create_color(VulkanContext& context, int width, int height);
    [[nodiscard]] static Result<CudaInteropImage> create_distance(VulkanContext& context, int width, int height);
    [[nodiscard]] Result<void> copy_from_cuda_array(const void* source_cuda_array, int width, int height,
                                                    std::uintptr_t stream);
    [[nodiscard]] VkImage image() const;
    // VK_NULL_HANDLE unless created with create_distance.
    [[nodiscard]] VkImageView view() const;
    [[nodiscard]] int width() const;
    [[nodiscard]] int height() const;

    void reset();

private:
    struct Spec;
    [[nodiscard]] static Result<CudaInteropImage> create(VulkanContext& context, int width, int height,
                                                         const Spec& spec);

    VkDevice device_ = VK_NULL_HANDLE;
    VkImage image_ = VK_NULL_HANDLE;
    VkDeviceMemory memory_ = VK_NULL_HANDLE;
    VkImageView view_ = VK_NULL_HANDLE;
    cudaExternalMemory_t cuda_memory_ = nullptr;
    cudaMipmappedArray_t mipmapped_array_ = nullptr;
    cudaArray_t cuda_array_ = nullptr;
    int width_ = 0;
    int height_ = 0;
};

} // namespace toi::viewport
