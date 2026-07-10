#include "toi/viewport/cuda_vulkan_interop.hpp"

#include <cstddef>
#include <cstdint>
#include <sstream>
#include <string>
#include <string_view>
#include <unistd.h>
#include <utility>

namespace toi::viewport {
namespace {

[[nodiscard]] std::string vk_error(std::string_view context, VkResult result)
{
    std::ostringstream out;
    out << context << " failed: VkResult " << static_cast<int>(result);
    return out.str();
}

[[nodiscard]] std::string cuda_error(std::string_view context, cudaError_t result)
{
    std::ostringstream out;
    out << context << " failed: " << cudaGetErrorString(result);
    return out.str();
}

[[nodiscard]] Result<void> require_cuda_success(cudaError_t result, std::string_view context)
{
    if (result != cudaSuccess) {
        return std::unexpected(make_error(cuda_error(context, result)));
    }
    return {};
}

[[nodiscard]] Result<void> require_vk_success(VkResult result, std::string_view context)
{
    if (result != VK_SUCCESS) {
        return std::unexpected(make_error(vk_error(context, result)));
    }
    return {};
}

[[nodiscard]] cudaStream_t cuda_stream_from_ovrtx(std::uintptr_t stream)
{
    if (stream <= std::uintptr_t{1}) {
        return nullptr;
    }
    return reinterpret_cast<cudaStream_t>(stream);
}

[[nodiscard]] Result<std::uint32_t> find_memory_type(VkPhysicalDevice physical_device, std::uint32_t type_filter,
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
    return std::unexpected(make_error("failed to find required Vulkan memory type"));
}

// One-shot transition of the interop image to GENERAL so CUDA can write it and
// Vulkan can blit from it without per-frame layout changes.
[[nodiscard]] Result<void> transition_to_general(VulkanContext& context, VkImage image)
{
    VkCommandBufferAllocateInfo allocate_info{};
    allocate_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocate_info.commandPool = context.command_pool();
    allocate_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocate_info.commandBufferCount = 1;

    VkCommandBuffer command_buffer = VK_NULL_HANDLE;
    if (auto result = require_vk_success(vkAllocateCommandBuffers(context.device(), &allocate_info, &command_buffer),
                                         "vkAllocateCommandBuffers");
        !result) {
        return result;
    }

    auto free_command_buffer = [&] {
        vkFreeCommandBuffers(context.device(), context.command_pool(), 1, &command_buffer);
    };

    VkCommandBufferBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (auto result = require_vk_success(vkBeginCommandBuffer(command_buffer, &begin_info), "vkBeginCommandBuffer");
        !result) {
        free_command_buffer();
        return result;
    }

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0,
                         nullptr, 0, nullptr, 1, &barrier);
    if (auto result = require_vk_success(vkEndCommandBuffer(command_buffer), "vkEndCommandBuffer"); !result) {
        free_command_buffer();
        return result;
    }

    VkFenceCreateInfo fence_info{};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    VkFence fence = VK_NULL_HANDLE;
    if (auto result = require_vk_success(vkCreateFence(context.device(), &fence_info, nullptr, &fence),
                                         "vkCreateFence");
        !result) {
        free_command_buffer();
        return result;
    }

    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &command_buffer;
    auto result = require_vk_success(vkQueueSubmit(context.graphics_queue(), 1, &submit, fence),
                                     "vkQueueSubmit interop layout");
    if (result) {
        result = require_vk_success(vkWaitForFences(context.device(), 1, &fence, VK_TRUE, UINT64_MAX),
                                    "vkWaitForFences interop layout");
    }
    vkDestroyFence(context.device(), fence, nullptr);
    free_command_buffer();
    return result;
}

} // namespace

Result<void> select_cuda_device(int cuda_device)
{
    return require_cuda_success(cudaSetDevice(cuda_device), "cudaSetDevice");
}

CudaInteropTimelineSemaphore::CudaInteropTimelineSemaphore(CudaInteropTimelineSemaphore&& other) noexcept
    : device_(std::exchange(other.device_, VK_NULL_HANDLE))
    , vk_semaphore_(std::exchange(other.vk_semaphore_, VK_NULL_HANDLE))
    , cuda_semaphore_(std::exchange(other.cuda_semaphore_, nullptr))
{
}

CudaInteropTimelineSemaphore& CudaInteropTimelineSemaphore::operator=(CudaInteropTimelineSemaphore&& other) noexcept
{
    if (this != &other) {
        reset();
        device_ = std::exchange(other.device_, VK_NULL_HANDLE);
        vk_semaphore_ = std::exchange(other.vk_semaphore_, VK_NULL_HANDLE);
        cuda_semaphore_ = std::exchange(other.cuda_semaphore_, nullptr);
    }
    return *this;
}

CudaInteropTimelineSemaphore::~CudaInteropTimelineSemaphore()
{
    reset();
}

Result<CudaInteropTimelineSemaphore> CudaInteropTimelineSemaphore::create(VulkanContext& context)
{
    VkSemaphoreTypeCreateInfo type_info{};
    type_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
    type_info.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    type_info.initialValue = 0;

    VkExportSemaphoreCreateInfo export_info{};
    export_info.sType = VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO;
    export_info.pNext = &type_info;
    export_info.handleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT;

    VkSemaphoreCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    create_info.pNext = &export_info;

    VkSemaphore semaphore = VK_NULL_HANDLE;
    if (auto result = require_vk_success(vkCreateSemaphore(context.device(), &create_info, nullptr, &semaphore),
                                         "vkCreateSemaphore");
        !result) {
        return std::unexpected(result.error());
    }

    auto get_fd =
        reinterpret_cast<PFN_vkGetSemaphoreFdKHR>(vkGetDeviceProcAddr(context.device(), "vkGetSemaphoreFdKHR"));
    if (get_fd == nullptr) {
        vkDestroySemaphore(context.device(), semaphore, nullptr);
        return std::unexpected(make_error("vkGetSemaphoreFdKHR is unavailable"));
    }

    VkSemaphoreGetFdInfoKHR fd_info{};
    fd_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR;
    fd_info.semaphore = semaphore;
    fd_info.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT;

    int fd = -1;
    if (auto result = require_vk_success(get_fd(context.device(), &fd_info, &fd), "vkGetSemaphoreFdKHR"); !result) {
        vkDestroySemaphore(context.device(), semaphore, nullptr);
        return std::unexpected(result.error());
    }

    cudaExternalSemaphoreHandleDesc desc{};
    desc.type = cudaExternalSemaphoreHandleTypeTimelineSemaphoreFd;
    desc.handle.fd = fd;

    cudaExternalSemaphore_t cuda_semaphore = nullptr;
    if (const auto import_result = cudaImportExternalSemaphore(&cuda_semaphore, &desc); import_result != cudaSuccess) {
        close(fd);
        vkDestroySemaphore(context.device(), semaphore, nullptr);
        return std::unexpected(make_error(cuda_error("cudaImportExternalSemaphore", import_result)));
    }

    CudaInteropTimelineSemaphore out;
    out.device_ = context.device();
    out.vk_semaphore_ = semaphore;
    out.cuda_semaphore_ = cuda_semaphore;
    return out;
}

Result<void> CudaInteropTimelineSemaphore::signal_from_ovrtx_stream(std::uint64_t value, std::uintptr_t stream)
{
    cudaExternalSemaphoreSignalParams params{};
    params.params.fence.value = value;
    return require_cuda_success(
        cudaSignalExternalSemaphoresAsync(&cuda_semaphore_, &params, 1, cuda_stream_from_ovrtx(stream)),
        "cudaSignalExternalSemaphoresAsync");
}

VkSemaphore CudaInteropTimelineSemaphore::vk_semaphore() const
{
    return vk_semaphore_;
}

void CudaInteropTimelineSemaphore::reset()
{
    if (cuda_semaphore_ != nullptr) {
        cudaDestroyExternalSemaphore(cuda_semaphore_);
        cuda_semaphore_ = nullptr;
    }
    if (device_ != VK_NULL_HANDLE && vk_semaphore_ != VK_NULL_HANDLE) {
        vkDestroySemaphore(device_, vk_semaphore_, nullptr);
        vk_semaphore_ = VK_NULL_HANDLE;
    }
}

CudaInteropImage::CudaInteropImage(CudaInteropImage&& other) noexcept
    : device_(std::exchange(other.device_, VK_NULL_HANDLE))
    , image_(std::exchange(other.image_, VK_NULL_HANDLE))
    , memory_(std::exchange(other.memory_, VK_NULL_HANDLE))
    , cuda_memory_(std::exchange(other.cuda_memory_, nullptr))
    , mipmapped_array_(std::exchange(other.mipmapped_array_, nullptr))
    , cuda_array_(std::exchange(other.cuda_array_, nullptr))
    , width_(std::exchange(other.width_, 0))
    , height_(std::exchange(other.height_, 0))
{
}

CudaInteropImage& CudaInteropImage::operator=(CudaInteropImage&& other) noexcept
{
    if (this != &other) {
        reset();
        device_ = std::exchange(other.device_, VK_NULL_HANDLE);
        image_ = std::exchange(other.image_, VK_NULL_HANDLE);
        memory_ = std::exchange(other.memory_, VK_NULL_HANDLE);
        cuda_memory_ = std::exchange(other.cuda_memory_, nullptr);
        mipmapped_array_ = std::exchange(other.mipmapped_array_, nullptr);
        cuda_array_ = std::exchange(other.cuda_array_, nullptr);
        width_ = std::exchange(other.width_, 0);
        height_ = std::exchange(other.height_, 0);
    }
    return *this;
}

CudaInteropImage::~CudaInteropImage()
{
    reset();
}

Result<CudaInteropImage> CudaInteropImage::create(VulkanContext& context, int width, int height)
{
    VkExternalMemoryImageCreateInfo external_info{};
    external_info.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;
    external_info.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;

    VkImageCreateInfo image_info{};
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.pNext = &external_info;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.format = VK_FORMAT_R8G8B8A8_UNORM;
    image_info.extent = {static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height), 1};
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VkImage image = VK_NULL_HANDLE;
    if (auto result = require_vk_success(vkCreateImage(context.device(), &image_info, nullptr, &image), "vkCreateImage");
        !result) {
        return std::unexpected(result.error());
    }

    VkMemoryRequirements memory_requirements{};
    vkGetImageMemoryRequirements(context.device(), image, &memory_requirements);
    auto memory_type = find_memory_type(context.physical_device(), memory_requirements.memoryTypeBits,
                                        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (!memory_type) {
        vkDestroyImage(context.device(), image, nullptr);
        return std::unexpected(memory_type.error());
    }

    VkExportMemoryAllocateInfo export_info{};
    export_info.sType = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO;
    export_info.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;

    VkMemoryDedicatedAllocateInfo dedicated_info{};
    dedicated_info.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO;
    dedicated_info.pNext = &export_info;
    dedicated_info.image = image;

    VkMemoryAllocateInfo allocate_info{};
    allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocate_info.pNext = &dedicated_info;
    allocate_info.allocationSize = memory_requirements.size;
    allocate_info.memoryTypeIndex = *memory_type;

    VkDeviceMemory memory = VK_NULL_HANDLE;
    if (auto result = require_vk_success(vkAllocateMemory(context.device(), &allocate_info, nullptr, &memory),
                                         "vkAllocateMemory");
        !result) {
        vkDestroyImage(context.device(), image, nullptr);
        return std::unexpected(result.error());
    }

    if (auto result = require_vk_success(vkBindImageMemory(context.device(), image, memory, 0), "vkBindImageMemory");
        !result) {
        vkFreeMemory(context.device(), memory, nullptr);
        vkDestroyImage(context.device(), image, nullptr);
        return std::unexpected(result.error());
    }

    auto get_fd = reinterpret_cast<PFN_vkGetMemoryFdKHR>(vkGetDeviceProcAddr(context.device(), "vkGetMemoryFdKHR"));
    if (get_fd == nullptr) {
        vkFreeMemory(context.device(), memory, nullptr);
        vkDestroyImage(context.device(), image, nullptr);
        return std::unexpected(make_error("vkGetMemoryFdKHR is unavailable"));
    }

    VkMemoryGetFdInfoKHR fd_info{};
    fd_info.sType = VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR;
    fd_info.memory = memory;
    fd_info.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;

    int fd = -1;
    if (auto result = require_vk_success(get_fd(context.device(), &fd_info, &fd), "vkGetMemoryFdKHR"); !result) {
        vkFreeMemory(context.device(), memory, nullptr);
        vkDestroyImage(context.device(), image, nullptr);
        return std::unexpected(result.error());
    }

    cudaExternalMemoryHandleDesc external_memory_desc{};
    external_memory_desc.type = cudaExternalMemoryHandleTypeOpaqueFd;
    external_memory_desc.handle.fd = fd;
    external_memory_desc.size = memory_requirements.size;

    cudaExternalMemory_t cuda_memory = nullptr;
    if (const auto import_result = cudaImportExternalMemory(&cuda_memory, &external_memory_desc);
        import_result != cudaSuccess) {
        close(fd);
        vkFreeMemory(context.device(), memory, nullptr);
        vkDestroyImage(context.device(), image, nullptr);
        return std::unexpected(make_error(cuda_error("cudaImportExternalMemory", import_result)));
    }

    cudaExternalMemoryMipmappedArrayDesc mipmapped_desc{};
    mipmapped_desc.offset = 0;
    mipmapped_desc.formatDesc = cudaCreateChannelDesc(8, 8, 8, 8, cudaChannelFormatKindUnsigned);
    mipmapped_desc.extent = cudaExtent{static_cast<std::size_t>(width), static_cast<std::size_t>(height), 0};
    mipmapped_desc.flags = cudaArrayColorAttachment;
    mipmapped_desc.numLevels = 1;

    cudaMipmappedArray_t mipmapped_array = nullptr;
    if (const auto cuda_result =
            cudaExternalMemoryGetMappedMipmappedArray(&mipmapped_array, cuda_memory, &mipmapped_desc);
        cuda_result != cudaSuccess) {
        cudaDestroyExternalMemory(cuda_memory);
        vkFreeMemory(context.device(), memory, nullptr);
        vkDestroyImage(context.device(), image, nullptr);
        return std::unexpected(make_error(cuda_error("cudaExternalMemoryGetMappedMipmappedArray", cuda_result)));
    }

    cudaArray_t cuda_array = nullptr;
    if (const auto cuda_result = cudaGetMipmappedArrayLevel(&cuda_array, mipmapped_array, 0);
        cuda_result != cudaSuccess) {
        cudaFreeMipmappedArray(mipmapped_array);
        cudaDestroyExternalMemory(cuda_memory);
        vkFreeMemory(context.device(), memory, nullptr);
        vkDestroyImage(context.device(), image, nullptr);
        return std::unexpected(make_error(cuda_error("cudaGetMipmappedArrayLevel", cuda_result)));
    }

    if (auto transitioned = transition_to_general(context, image); !transitioned) {
        cudaFreeMipmappedArray(mipmapped_array);
        cudaDestroyExternalMemory(cuda_memory);
        vkFreeMemory(context.device(), memory, nullptr);
        vkDestroyImage(context.device(), image, nullptr);
        return std::unexpected(transitioned.error());
    }

    CudaInteropImage out;
    out.device_ = context.device();
    out.image_ = image;
    out.memory_ = memory;
    out.cuda_memory_ = cuda_memory;
    out.mipmapped_array_ = mipmapped_array;
    out.cuda_array_ = cuda_array;
    out.width_ = width;
    out.height_ = height;
    return out;
}

Result<void> CudaInteropImage::copy_from_cuda_array(const void* source_cuda_array, int width, int height,
                                                    std::uintptr_t stream)
{
    if (source_cuda_array == nullptr || cuda_array_ == nullptr || width != width_ || height != height_) {
        return std::unexpected(make_error("CUDA array does not match Vulkan interop image"));
    }

    cudaMemcpy3DParms copy{};
    copy.srcArray = reinterpret_cast<cudaArray_t>(const_cast<void*>(source_cuda_array));
    copy.dstArray = cuda_array_;
    copy.extent = cudaExtent{
        .width = static_cast<std::size_t>(width_),
        .height = static_cast<std::size_t>(height_),
        .depth = 1,
    };
    copy.kind = cudaMemcpyDeviceToDevice;
    return require_cuda_success(cudaMemcpy3DAsync(&copy, cuda_stream_from_ovrtx(stream)), "cudaMemcpy3DAsync");
}

VkImage CudaInteropImage::image() const
{
    return image_;
}

int CudaInteropImage::width() const
{
    return width_;
}

int CudaInteropImage::height() const
{
    return height_;
}

void CudaInteropImage::reset()
{
    if (device_ != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device_);
    }
    if (mipmapped_array_ != nullptr) {
        cudaFreeMipmappedArray(mipmapped_array_);
        mipmapped_array_ = nullptr;
        cuda_array_ = nullptr;
    }
    if (cuda_memory_ != nullptr) {
        cudaDestroyExternalMemory(cuda_memory_);
        cuda_memory_ = nullptr;
    }
    if (device_ != VK_NULL_HANDLE && image_ != VK_NULL_HANDLE) {
        vkDestroyImage(device_, image_, nullptr);
        image_ = VK_NULL_HANDLE;
    }
    if (device_ != VK_NULL_HANDLE && memory_ != VK_NULL_HANDLE) {
        vkFreeMemory(device_, memory_, nullptr);
        memory_ = VK_NULL_HANDLE;
    }
    device_ = VK_NULL_HANDLE;
}

CudaInteropFloatImage::CudaInteropFloatImage(CudaInteropFloatImage&& other) noexcept
    : device_(std::exchange(other.device_, VK_NULL_HANDLE))
    , image_(std::exchange(other.image_, VK_NULL_HANDLE))
    , memory_(std::exchange(other.memory_, VK_NULL_HANDLE))
    , view_(std::exchange(other.view_, VK_NULL_HANDLE))
    , cuda_memory_(std::exchange(other.cuda_memory_, nullptr))
    , mipmapped_array_(std::exchange(other.mipmapped_array_, nullptr))
    , cuda_array_(std::exchange(other.cuda_array_, nullptr))
    , width_(std::exchange(other.width_, 0))
    , height_(std::exchange(other.height_, 0))
{
}

CudaInteropFloatImage& CudaInteropFloatImage::operator=(CudaInteropFloatImage&& other) noexcept
{
    if (this != &other) {
        reset();
        device_ = std::exchange(other.device_, VK_NULL_HANDLE);
        image_ = std::exchange(other.image_, VK_NULL_HANDLE);
        memory_ = std::exchange(other.memory_, VK_NULL_HANDLE);
        view_ = std::exchange(other.view_, VK_NULL_HANDLE);
        cuda_memory_ = std::exchange(other.cuda_memory_, nullptr);
        mipmapped_array_ = std::exchange(other.mipmapped_array_, nullptr);
        cuda_array_ = std::exchange(other.cuda_array_, nullptr);
        width_ = std::exchange(other.width_, 0);
        height_ = std::exchange(other.height_, 0);
    }
    return *this;
}

CudaInteropFloatImage::~CudaInteropFloatImage()
{
    reset();
}

Result<CudaInteropFloatImage> CudaInteropFloatImage::create(VulkanContext& context, int width, int height)
{
    VkExternalMemoryImageCreateInfo external_info{};
    external_info.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;
    external_info.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;

    VkImageCreateInfo image_info{};
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.pNext = &external_info;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.format = VK_FORMAT_R32_SFLOAT;
    image_info.extent = {static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height), 1};
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VkImage image = VK_NULL_HANDLE;
    if (auto result = require_vk_success(vkCreateImage(context.device(), &image_info, nullptr, &image), "vkCreateImage");
        !result) {
        return std::unexpected(result.error());
    }

    VkMemoryRequirements memory_requirements{};
    vkGetImageMemoryRequirements(context.device(), image, &memory_requirements);
    auto memory_type = find_memory_type(context.physical_device(), memory_requirements.memoryTypeBits,
                                        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (!memory_type) {
        vkDestroyImage(context.device(), image, nullptr);
        return std::unexpected(memory_type.error());
    }

    VkExportMemoryAllocateInfo export_info{};
    export_info.sType = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO;
    export_info.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;

    VkMemoryDedicatedAllocateInfo dedicated_info{};
    dedicated_info.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO;
    dedicated_info.pNext = &export_info;
    dedicated_info.image = image;

    VkMemoryAllocateInfo allocate_info{};
    allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocate_info.pNext = &dedicated_info;
    allocate_info.allocationSize = memory_requirements.size;
    allocate_info.memoryTypeIndex = *memory_type;

    VkDeviceMemory memory = VK_NULL_HANDLE;
    if (auto result = require_vk_success(vkAllocateMemory(context.device(), &allocate_info, nullptr, &memory),
                                         "vkAllocateMemory");
        !result) {
        vkDestroyImage(context.device(), image, nullptr);
        return std::unexpected(result.error());
    }
    if (auto result = require_vk_success(vkBindImageMemory(context.device(), image, memory, 0), "vkBindImageMemory");
        !result) {
        vkFreeMemory(context.device(), memory, nullptr);
        vkDestroyImage(context.device(), image, nullptr);
        return std::unexpected(result.error());
    }

    VkImageViewCreateInfo view_info{};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image = image;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = VK_FORMAT_R32_SFLOAT;
    view_info.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VkImageView view = VK_NULL_HANDLE;
    if (auto result = require_vk_success(vkCreateImageView(context.device(), &view_info, nullptr, &view),
                                         "vkCreateImageView");
        !result) {
        vkFreeMemory(context.device(), memory, nullptr);
        vkDestroyImage(context.device(), image, nullptr);
        return std::unexpected(result.error());
    }

    auto get_fd = reinterpret_cast<PFN_vkGetMemoryFdKHR>(vkGetDeviceProcAddr(context.device(), "vkGetMemoryFdKHR"));
    if (get_fd == nullptr) {
        vkDestroyImageView(context.device(), view, nullptr);
        vkFreeMemory(context.device(), memory, nullptr);
        vkDestroyImage(context.device(), image, nullptr);
        return std::unexpected(make_error("vkGetMemoryFdKHR is unavailable"));
    }
    VkMemoryGetFdInfoKHR fd_info{};
    fd_info.sType = VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR;
    fd_info.memory = memory;
    fd_info.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
    int fd = -1;
    if (auto result = require_vk_success(get_fd(context.device(), &fd_info, &fd), "vkGetMemoryFdKHR"); !result) {
        vkDestroyImageView(context.device(), view, nullptr);
        vkFreeMemory(context.device(), memory, nullptr);
        vkDestroyImage(context.device(), image, nullptr);
        return std::unexpected(result.error());
    }

    cudaExternalMemoryHandleDesc external_memory_desc{};
    external_memory_desc.type = cudaExternalMemoryHandleTypeOpaqueFd;
    external_memory_desc.handle.fd = fd;
    external_memory_desc.size = memory_requirements.size;
    cudaExternalMemory_t cuda_memory = nullptr;
    if (const auto import_result = cudaImportExternalMemory(&cuda_memory, &external_memory_desc);
        import_result != cudaSuccess) {
        close(fd);
        vkDestroyImageView(context.device(), view, nullptr);
        vkFreeMemory(context.device(), memory, nullptr);
        vkDestroyImage(context.device(), image, nullptr);
        return std::unexpected(make_error(cuda_error("cudaImportExternalMemory", import_result)));
    }

    cudaExternalMemoryMipmappedArrayDesc mipmapped_desc{};
    mipmapped_desc.offset = 0;
    mipmapped_desc.formatDesc = cudaCreateChannelDesc(32, 0, 0, 0, cudaChannelFormatKindFloat);
    mipmapped_desc.extent = cudaExtent{static_cast<std::size_t>(width), static_cast<std::size_t>(height), 0};
    mipmapped_desc.numLevels = 1;
    cudaMipmappedArray_t mipmapped_array = nullptr;
    if (const auto cuda_result =
            cudaExternalMemoryGetMappedMipmappedArray(&mipmapped_array, cuda_memory, &mipmapped_desc);
        cuda_result != cudaSuccess) {
        cudaDestroyExternalMemory(cuda_memory);
        vkDestroyImageView(context.device(), view, nullptr);
        vkFreeMemory(context.device(), memory, nullptr);
        vkDestroyImage(context.device(), image, nullptr);
        return std::unexpected(make_error(cuda_error("cudaExternalMemoryGetMappedMipmappedArray", cuda_result)));
    }
    cudaArray_t cuda_array = nullptr;
    if (const auto cuda_result = cudaGetMipmappedArrayLevel(&cuda_array, mipmapped_array, 0);
        cuda_result != cudaSuccess) {
        cudaFreeMipmappedArray(mipmapped_array);
        cudaDestroyExternalMemory(cuda_memory);
        vkDestroyImageView(context.device(), view, nullptr);
        vkFreeMemory(context.device(), memory, nullptr);
        vkDestroyImage(context.device(), image, nullptr);
        return std::unexpected(make_error(cuda_error("cudaGetMipmappedArrayLevel", cuda_result)));
    }

    if (auto transitioned = transition_to_general(context, image); !transitioned) {
        cudaFreeMipmappedArray(mipmapped_array);
        cudaDestroyExternalMemory(cuda_memory);
        vkDestroyImageView(context.device(), view, nullptr);
        vkFreeMemory(context.device(), memory, nullptr);
        vkDestroyImage(context.device(), image, nullptr);
        return std::unexpected(transitioned.error());
    }

    CudaInteropFloatImage out;
    out.device_ = context.device();
    out.image_ = image;
    out.memory_ = memory;
    out.view_ = view;
    out.cuda_memory_ = cuda_memory;
    out.mipmapped_array_ = mipmapped_array;
    out.cuda_array_ = cuda_array;
    out.width_ = width;
    out.height_ = height;
    return out;
}

Result<void> CudaInteropFloatImage::copy_from_cuda_array(const void* source_cuda_array, int width, int height,
                                                         std::uintptr_t stream)
{
    if (source_cuda_array == nullptr || cuda_array_ == nullptr || width != width_ || height != height_) {
        return std::unexpected(make_error("CUDA array does not match Vulkan interop float image"));
    }
    cudaMemcpy3DParms copy{};
    copy.srcArray = reinterpret_cast<cudaArray_t>(const_cast<void*>(source_cuda_array));
    copy.dstArray = cuda_array_;
    copy.extent = cudaExtent{static_cast<std::size_t>(width_), static_cast<std::size_t>(height_), 1};
    copy.kind = cudaMemcpyDeviceToDevice;
    return require_cuda_success(cudaMemcpy3DAsync(&copy, cuda_stream_from_ovrtx(stream)), "cudaMemcpy3DAsync");
}

VkImage CudaInteropFloatImage::image() const
{
    return image_;
}
VkImageView CudaInteropFloatImage::view() const
{
    return view_;
}
int CudaInteropFloatImage::width() const
{
    return width_;
}
int CudaInteropFloatImage::height() const
{
    return height_;
}

void CudaInteropFloatImage::reset()
{
    if (device_ != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device_);
    }
    if (mipmapped_array_ != nullptr) {
        cudaFreeMipmappedArray(mipmapped_array_);
        mipmapped_array_ = nullptr;
        cuda_array_ = nullptr;
    }
    if (cuda_memory_ != nullptr) {
        cudaDestroyExternalMemory(cuda_memory_);
        cuda_memory_ = nullptr;
    }
    if (device_ != VK_NULL_HANDLE && view_ != VK_NULL_HANDLE) {
        vkDestroyImageView(device_, view_, nullptr);
        view_ = VK_NULL_HANDLE;
    }
    if (device_ != VK_NULL_HANDLE && image_ != VK_NULL_HANDLE) {
        vkDestroyImage(device_, image_, nullptr);
        image_ = VK_NULL_HANDLE;
    }
    if (device_ != VK_NULL_HANDLE && memory_ != VK_NULL_HANDLE) {
        vkFreeMemory(device_, memory_, nullptr);
        memory_ = VK_NULL_HANDLE;
    }
    device_ = VK_NULL_HANDLE;
}

} // namespace toi::viewport
