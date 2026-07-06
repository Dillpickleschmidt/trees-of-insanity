#include "toi/viewport/vulkan_swapchain.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <sstream>
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

struct SwapchainSupport {
    VkSurfaceCapabilitiesKHR capabilities{};
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> present_modes;
};

[[nodiscard]] SwapchainSupport query_swapchain_support(VulkanContext& context)
{
    SwapchainSupport support;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(context.physical_device(), context.surface(), &support.capabilities);

    std::uint32_t format_count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(context.physical_device(), context.surface(), &format_count, nullptr);
    support.formats.resize(format_count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(context.physical_device(), context.surface(), &format_count,
                                         support.formats.data());

    std::uint32_t present_mode_count = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(context.physical_device(), context.surface(), &present_mode_count,
                                              nullptr);
    support.present_modes.resize(present_mode_count);
    vkGetPhysicalDeviceSurfacePresentModesKHR(context.physical_device(), context.surface(), &present_mode_count,
                                              support.present_modes.data());

    return support;
}

[[nodiscard]] VkSurfaceFormatKHR choose_surface_format(const std::vector<VkSurfaceFormatKHR>& formats)
{
    for (const auto& format : formats) {
        if (format.format == VK_FORMAT_B8G8R8A8_UNORM && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return format;
        }
    }
    for (const auto& format : formats) {
        if (format.format == VK_FORMAT_R8G8B8A8_UNORM && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return format;
        }
    }
    return formats.front();
}

[[nodiscard]] VkPresentModeKHR choose_present_mode(const std::vector<VkPresentModeKHR>& present_modes)
{
    for (const auto mode : present_modes) {
        if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
            return mode;
        }
    }
    return VK_PRESENT_MODE_FIFO_KHR;
}

[[nodiscard]] VkExtent2D choose_extent(const VkSurfaceCapabilitiesKHR& capabilities, int width, int height)
{
    if (capabilities.currentExtent.width != std::numeric_limits<std::uint32_t>::max()) {
        return capabilities.currentExtent;
    }

    VkExtent2D extent{
        .width = static_cast<std::uint32_t>(std::max(1, width)),
        .height = static_cast<std::uint32_t>(std::max(1, height)),
    };
    extent.width = std::clamp(extent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
    extent.height = std::clamp(extent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
    return extent;
}

} // namespace

Result<VulkanSwapchain> VulkanSwapchain::create(VulkanContext& context, int width, int height)
{
    auto support = query_swapchain_support(context);
    if (support.formats.empty() || support.present_modes.empty()) {
        return std::unexpected(make_error("Vulkan surface does not support swapchains"));
    }

    const auto surface_format = choose_surface_format(support.formats);
    const auto present_mode = choose_present_mode(support.present_modes);
    const auto extent = choose_extent(support.capabilities, width, height);

    std::uint32_t image_count = support.capabilities.minImageCount + 1;
    if (support.capabilities.maxImageCount > 0) {
        image_count = std::min(image_count, support.capabilities.maxImageCount);
    }

    VkSwapchainCreateInfoKHR create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    create_info.surface = context.surface();
    create_info.minImageCount = image_count;
    create_info.imageFormat = surface_format.format;
    create_info.imageColorSpace = surface_format.colorSpace;
    create_info.imageExtent = extent;
    create_info.imageArrayLayers = 1;
    create_info.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    create_info.preTransform = support.capabilities.currentTransform;
    create_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    create_info.presentMode = present_mode;
    create_info.clipped = VK_TRUE;
    create_info.oldSwapchain = VK_NULL_HANDLE;

    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    const auto result = vkCreateSwapchainKHR(context.device(), &create_info, nullptr, &swapchain);
    if (result != VK_SUCCESS) {
        return std::unexpected(make_error(vk_error("vkCreateSwapchainKHR", result)));
    }

    std::uint32_t actual_image_count = 0;
    vkGetSwapchainImagesKHR(context.device(), swapchain, &actual_image_count, nullptr);
    std::vector<VkImage> images(actual_image_count);
    vkGetSwapchainImagesKHR(context.device(), swapchain, &actual_image_count, images.data());

    VulkanSwapchain out;
    out.context_ = &context;
    out.swapchain_ = swapchain;
    out.extent_ = extent;
    out.format_ = surface_format.format;
    out.images_ = std::move(images);
    return out;
}

VulkanSwapchain::VulkanSwapchain(VulkanSwapchain&& other) noexcept
    : context_(std::exchange(other.context_, nullptr))
    , swapchain_(std::exchange(other.swapchain_, VK_NULL_HANDLE))
    , extent_(std::exchange(other.extent_, {}))
    , format_(std::exchange(other.format_, VK_FORMAT_UNDEFINED))
    , images_(std::move(other.images_))
{
}

VulkanSwapchain& VulkanSwapchain::operator=(VulkanSwapchain&& other) noexcept
{
    if (this != &other) {
        reset();
        context_ = std::exchange(other.context_, nullptr);
        swapchain_ = std::exchange(other.swapchain_, VK_NULL_HANDLE);
        extent_ = std::exchange(other.extent_, {});
        format_ = std::exchange(other.format_, VK_FORMAT_UNDEFINED);
        images_ = std::move(other.images_);
    }
    return *this;
}

VulkanSwapchain::~VulkanSwapchain()
{
    reset();
}

VkSwapchainKHR VulkanSwapchain::get() const
{
    return swapchain_;
}
VkExtent2D VulkanSwapchain::extent() const
{
    return extent_;
}
VkFormat VulkanSwapchain::format() const
{
    return format_;
}
const std::vector<VkImage>& VulkanSwapchain::images() const
{
    return images_;
}

void VulkanSwapchain::reset()
{
    if (context_ != nullptr && swapchain_ != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(context_->device(), swapchain_, nullptr);
        swapchain_ = VK_NULL_HANDLE;
    }
    format_ = VK_FORMAT_UNDEFINED;
}

} // namespace toi::viewport
