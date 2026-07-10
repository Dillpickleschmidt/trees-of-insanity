#include "toi/viewport/vulkan_presenter.hpp"

#include <algorithm>
#include <string>
#include <utility>

namespace toi::viewport {
namespace {

[[nodiscard]] Result<void> require_vk(VkResult result, const char* operation)
{
    if (result == VK_SUCCESS) {
        return {};
    }
    return std::unexpected(make_error(std::string(operation) + " failed: VkResult " + std::to_string(result)));
}

} // namespace

Result<std::unique_ptr<VulkanPresenter>> VulkanPresenter::attach(std::uintptr_t native_window, int width, int height,
                                                                 int cuda_device)
{
    auto surface = NativeSurface::attach(native_window);
    if (!surface) {
        return std::unexpected(surface.error());
    }
    auto surface_extent = surface->extent();
    if (!surface_extent) {
        return std::unexpected(surface_extent.error());
    }
    const int initial_width = surface_extent->width > 1 ? surface_extent->width : std::max(1, width);
    const int initial_height = surface_extent->height > 1 ? surface_extent->height : std::max(1, height);

    auto context = VulkanContext::create(*surface, cuda_device);
    if (!context) {
        return std::unexpected(context.error());
    }

    auto presenter = std::unique_ptr<VulkanPresenter>(new VulkanPresenter());
    presenter->surface_ = std::move(*surface);
    presenter->context_ = std::move(*context);

    auto swapchain = VulkanSwapchain::create(presenter->context_, initial_width, initial_height);
    if (!swapchain) {
        return std::unexpected(swapchain.error());
    }
    presenter->swapchain_ = std::move(*swapchain);
    if (auto resources = presenter->create_frame_resources(); !resources) {
        return std::unexpected(resources.error());
    }
    return presenter;
}

VulkanPresenter::~VulkanPresenter()
{
    if (context_.device() != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(context_.device());
    }
    destroy_frame_resources();
}

Result<void> VulkanPresenter::wait_for_frame_slot()
{
    return require_vk(vkWaitForFences(context_.device(), 1, &in_flight_, VK_TRUE, UINT64_MAX), "vkWaitForFences");
}

Result<std::optional<AcquiredViewportFrame>> VulkanPresenter::acquire_frame()
{
    std::uint32_t image_index = 0;
    const VkResult acquired = vkAcquireNextImageKHR(context_.device(), swapchain_.get(), UINT64_MAX, image_available_,
                                                    VK_NULL_HANDLE, &image_index);
    if (acquired == VK_ERROR_OUT_OF_DATE_KHR) {
        return std::optional<AcquiredViewportFrame>{};
    }
    if (acquired != VK_SUCCESS && acquired != VK_SUBOPTIMAL_KHR) {
        return std::unexpected(make_error("vkAcquireNextImageKHR failed: VkResult " + std::to_string(acquired)));
    }
    if (auto reset = require_vk(vkResetFences(context_.device(), 1, &in_flight_), "vkResetFences"); !reset) {
        return std::unexpected(reset.error());
    }

    VkCommandBuffer command_buffer = command_buffers_[image_index];
    if (auto reset = require_vk(vkResetCommandBuffer(command_buffer, 0), "vkResetCommandBuffer"); !reset) {
        return std::unexpected(reset.error());
    }
    VkCommandBufferBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (auto begun = require_vk(vkBeginCommandBuffer(command_buffer, &begin_info), "vkBeginCommandBuffer"); !begun) {
        return std::unexpected(begun.error());
    }

    return AcquiredViewportFrame{
        .command_buffer = command_buffer,
        .image = swapchain_.images()[image_index],
        .image_index = image_index,
        .swapchain_suboptimal = acquired == VK_SUBOPTIMAL_KHR,
    };
}

Result<bool> VulkanPresenter::submit_and_present(const AcquiredViewportFrame& frame,
                                                 ViewportTimelineWait timeline_wait)
{
    if (auto ended = require_vk(vkEndCommandBuffer(frame.command_buffer), "vkEndCommandBuffer"); !ended) {
        return std::unexpected(ended.error());
    }

    const VkSemaphore wait_semaphores[2] = {image_available_, timeline_wait.semaphore};
    const VkPipelineStageFlags wait_stages[2] = {VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, timeline_wait.stages};
    const std::uint64_t wait_values[2] = {0, timeline_wait.value};
    VkTimelineSemaphoreSubmitInfo timeline_info{};
    timeline_info.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
    timeline_info.waitSemaphoreValueCount = 2;
    timeline_info.pWaitSemaphoreValues = wait_values;

    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.pNext = &timeline_info;
    submit.waitSemaphoreCount = 2;
    submit.pWaitSemaphores = wait_semaphores;
    submit.pWaitDstStageMask = wait_stages;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &frame.command_buffer;
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = &render_finished_[frame.image_index];
    if (auto submitted = require_vk(vkQueueSubmit(context_.graphics_queue(), 1, &submit, in_flight_), "vkQueueSubmit");
        !submitted) {
        return std::unexpected(submitted.error());
    }

    VkSwapchainKHR swapchain = swapchain_.get();
    VkPresentInfoKHR present{};
    present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &render_finished_[frame.image_index];
    present.swapchainCount = 1;
    present.pSwapchains = &swapchain;
    present.pImageIndices = &frame.image_index;
    const VkResult presented = vkQueuePresentKHR(context_.graphics_queue(), &present);
    if (presented != VK_SUCCESS && presented != VK_ERROR_OUT_OF_DATE_KHR && presented != VK_SUBOPTIMAL_KHR) {
        return std::unexpected(make_error("vkQueuePresentKHR failed: VkResult " + std::to_string(presented)));
    }
    return frame.swapchain_suboptimal || presented == VK_ERROR_OUT_OF_DATE_KHR || presented == VK_SUBOPTIMAL_KHR;
}

Result<void> VulkanPresenter::recreate_swapchain()
{
    if (auto idle = wait_idle(); !idle) {
        return std::unexpected(idle.error());
    }
    auto surface_extent = surface_.extent();
    if (!surface_extent) {
        return std::unexpected(surface_extent.error());
    }

    destroy_frame_resources();
    swapchain_.reset();
    auto replacement = VulkanSwapchain::create(context_, surface_extent->width, surface_extent->height);
    if (!replacement) {
        return std::unexpected(replacement.error());
    }
    swapchain_ = std::move(*replacement);
    return create_frame_resources();
}

Result<void> VulkanPresenter::wait_idle()
{
    return require_vk(vkDeviceWaitIdle(context_.device()), "vkDeviceWaitIdle");
}

VulkanContext& VulkanPresenter::context()
{
    return context_;
}

VulkanSwapchain& VulkanPresenter::swapchain()
{
    return swapchain_;
}

const VulkanSwapchain& VulkanPresenter::swapchain() const
{
    return swapchain_;
}

const VulkanContextInfo& VulkanPresenter::context_info() const
{
    return context_.info();
}

Result<void> VulkanPresenter::create_frame_resources()
{
    VkSemaphoreCreateInfo semaphore_info{};
    semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    if (auto created = require_vk(vkCreateSemaphore(context_.device(), &semaphore_info, nullptr, &image_available_),
                                  "vkCreateSemaphore");
        !created) {
        return std::unexpected(created.error());
    }

    VkFenceCreateInfo fence_info{};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    if (auto created = require_vk(vkCreateFence(context_.device(), &fence_info, nullptr, &in_flight_), "vkCreateFence");
        !created) {
        return std::unexpected(created.error());
    }

    render_finished_.resize(swapchain_.images().size(), VK_NULL_HANDLE);
    for (auto& semaphore : render_finished_) {
        if (auto created = require_vk(vkCreateSemaphore(context_.device(), &semaphore_info, nullptr, &semaphore),
                                      "vkCreateSemaphore");
            !created) {
            return std::unexpected(created.error());
        }
    }

    command_buffers_.resize(swapchain_.images().size(), VK_NULL_HANDLE);
    VkCommandBufferAllocateInfo allocate_info{};
    allocate_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocate_info.commandPool = context_.command_pool();
    allocate_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocate_info.commandBufferCount = static_cast<std::uint32_t>(command_buffers_.size());
    return require_vk(vkAllocateCommandBuffers(context_.device(), &allocate_info, command_buffers_.data()),
                      "vkAllocateCommandBuffers");
}

void VulkanPresenter::destroy_frame_resources()
{
    if (context_.device() == VK_NULL_HANDLE) {
        return;
    }
    if (!command_buffers_.empty()) {
        vkFreeCommandBuffers(context_.device(), context_.command_pool(),
                             static_cast<std::uint32_t>(command_buffers_.size()), command_buffers_.data());
        command_buffers_.clear();
    }
    for (VkSemaphore semaphore : render_finished_) {
        if (semaphore != VK_NULL_HANDLE) {
            vkDestroySemaphore(context_.device(), semaphore, nullptr);
        }
    }
    render_finished_.clear();
    if (image_available_ != VK_NULL_HANDLE) {
        vkDestroySemaphore(context_.device(), image_available_, nullptr);
        image_available_ = VK_NULL_HANDLE;
    }
    if (in_flight_ != VK_NULL_HANDLE) {
        vkDestroyFence(context_.device(), in_flight_, nullptr);
        in_flight_ = VK_NULL_HANDLE;
    }
}

} // namespace toi::viewport
