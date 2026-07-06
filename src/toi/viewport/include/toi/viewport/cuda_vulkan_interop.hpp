#pragma once

#include "toi/viewport/error.hpp"
#include "toi/viewport/vulkan_context.hpp"

#include <cuda_runtime_api.h>
#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>

namespace toi::viewport {

// A view of a device-resident CUDA color frame (the ovrtx LdrColor tensor).
struct CudaDeviceFrameView {
    const void* device_data = nullptr;
    int width = 0;
    int height = 0;
    int channel_count = 0;
    std::size_t row_stride_bytes = 0;
    std::size_t byte_offset = 0;
    std::uintptr_t stream = 1;
};

[[nodiscard]] Result<void> select_cuda_device(int cuda_device);

// A Vulkan semaphore shared with CUDA (opaque fd). CUDA signals it when its
// copy into the shared image completes; the Vulkan submit waits on it, giving
// the cross-engine memory dependency a CPU sync alone does not provide.
class CudaInteropSemaphore {
public:
    CudaInteropSemaphore() = default;
    CudaInteropSemaphore(const CudaInteropSemaphore&) = delete;
    CudaInteropSemaphore& operator=(const CudaInteropSemaphore&) = delete;
    CudaInteropSemaphore(CudaInteropSemaphore&& other) noexcept;
    CudaInteropSemaphore& operator=(CudaInteropSemaphore&& other) noexcept;
    ~CudaInteropSemaphore();

    [[nodiscard]] static Result<CudaInteropSemaphore> create(VulkanContext& context);
    [[nodiscard]] Result<void> signal_from_ovrtx_stream(std::uintptr_t stream);
    [[nodiscard]] VkSemaphore vk_semaphore() const;

    void reset();

private:
    VkDevice device_ = VK_NULL_HANDLE;
    VkSemaphore vk_semaphore_ = VK_NULL_HANDLE;
    cudaExternalSemaphore_t cuda_semaphore_ = nullptr;
};

// An RGBA8 Vulkan image whose device memory is shared with a CUDA array via an
// opaque-fd external-memory handle. CUDA writes the frame; Vulkan blits it to
// the swapchain.
class CudaInteropImage {
public:
    CudaInteropImage() = default;
    CudaInteropImage(const CudaInteropImage&) = delete;
    CudaInteropImage& operator=(const CudaInteropImage&) = delete;
    CudaInteropImage(CudaInteropImage&& other) noexcept;
    CudaInteropImage& operator=(CudaInteropImage&& other) noexcept;
    ~CudaInteropImage();

    [[nodiscard]] static Result<CudaInteropImage> create(VulkanContext& context, int width, int height);
    [[nodiscard]] Result<void> copy_from_cuda_frame(const CudaDeviceFrameView& frame);
    [[nodiscard]] VkImage image() const;
    [[nodiscard]] int width() const;
    [[nodiscard]] int height() const;

    void reset();

private:
    VulkanContext* context_ = nullptr;
    VkImage image_ = VK_NULL_HANDLE;
    VkDeviceMemory memory_ = VK_NULL_HANDLE;
    cudaExternalMemory_t cuda_memory_ = nullptr;
    cudaMipmappedArray_t mipmapped_array_ = nullptr;
    cudaArray_t cuda_array_ = nullptr;
    int width_ = 0;
    int height_ = 0;
};

} // namespace toi::viewport
