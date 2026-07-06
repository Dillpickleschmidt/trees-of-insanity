#include "toi/viewport/viewport_session.hpp"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <utility>

namespace toi::viewport {
namespace {

[[nodiscard]] VkImageSubresourceRange whole_color_image()
{
    return VkImageSubresourceRange{
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel = 0,
        .levelCount = 1,
        .baseArrayLayer = 0,
        .layerCount = 1,
    };
}

void transition(VkCommandBuffer command_buffer, VkImage image, VkImageLayout old_layout, VkImageLayout new_layout,
                VkAccessFlags src_access, VkAccessFlags dst_access, VkPipelineStageFlags src_stage,
                VkPipelineStageFlags dst_stage)
{
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = old_layout;
    barrier.newLayout = new_layout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = whole_color_image();
    barrier.srcAccessMask = src_access;
    barrier.dstAccessMask = dst_access;
    vkCmdPipelineBarrier(command_buffer, src_stage, dst_stage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

// A slow hue cycle so the frame is always a clearly non-black color and its
// motion confirms the present loop is live.
[[nodiscard]] VkClearColorValue test_pattern_color(std::uint64_t frame)
{
    const float t = static_cast<float>(frame) * 0.02F;
    return VkClearColorValue{
        .float32 = {
            0.20F + 0.20F * std::sin(t),
            0.45F + 0.30F * std::sin(t + 2.094F),
            0.35F + 0.25F * std::sin(t + 4.188F),
            1.0F,
        }};
}

} // namespace

Result<std::unique_ptr<ViewportSession>> ViewportSession::attach(unsigned long x_window, int width, int height)
{
    auto connection = X11Connection::open();
    if (!connection) {
        return std::unexpected(connection.error());
    }

    auto handle = connection->surface_handle(x_window);
    if (!handle) {
        return std::unexpected(handle.error());
    }
    const int initial_width = handle->width > 1 ? handle->width : (width > 0 ? width : 1);
    const int initial_height = handle->height > 1 ? handle->height : (height > 0 ? height : 1);

    auto context = VulkanContext::create(*handle);
    if (!context) {
        return std::unexpected(context.error());
    }

    auto swapchain = VulkanSwapchain::create(*context, initial_width, initial_height);
    if (!swapchain) {
        return std::unexpected(swapchain.error());
    }

    auto session = std::unique_ptr<ViewportSession>(new ViewportSession());
    session->connection_ = std::move(*connection);
    session->context_ = std::move(*context);
    session->swapchain_ = std::move(*swapchain);
    session->x_window_ = x_window;

    if (auto sync = session->create_sync_objects(); !sync) {
        return std::unexpected(sync.error());
    }
    if (auto resources = session->create_swapchain_resources(); !resources) {
        return std::unexpected(resources.error());
    }

    session->info_ = ViewportInfo{
        .device_name = session->context_.info().physical_device_name,
        .width = static_cast<int>(session->swapchain_.extent().width),
        .height = static_cast<int>(session->swapchain_.extent().height),
    };

    session->running_ = true;
    session->thread_ = std::thread(&ViewportSession::render_loop, session.get());
    return session;
}

ViewportSession::~ViewportSession()
{
    running_ = false;
    if (thread_.joinable()) {
        thread_.join();
    }

    VkDevice device = context_.device();
    if (device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device);
        destroy_swapchain_resources();
        for (VkSemaphore semaphore : image_available_) {
            if (semaphore != VK_NULL_HANDLE) {
                vkDestroySemaphore(device, semaphore, nullptr);
            }
        }
        for (VkFence fence : in_flight_) {
            if (fence != VK_NULL_HANDLE) {
                vkDestroyFence(device, fence, nullptr);
            }
        }
    }
    // swapchain_, context_, connection_ release in reverse declaration order:
    // swapchain first, then device/surface/instance, then the X connection.
}

const ViewportInfo& ViewportSession::info() const
{
    return info_;
}

Result<void> ViewportSession::create_sync_objects()
{
    VkDevice device = context_.device();
    VkSemaphoreCreateInfo semaphore_info{};
    semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkFenceCreateInfo fence_info{};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (int frame = 0; frame < kFramesInFlight; ++frame) {
        if (vkCreateSemaphore(device, &semaphore_info, nullptr, &image_available_[frame]) != VK_SUCCESS ||
            vkCreateFence(device, &fence_info, nullptr, &in_flight_[frame]) != VK_SUCCESS) {
            return std::unexpected(make_error("failed to create per-frame sync objects"));
        }
    }
    return {};
}

Result<void> ViewportSession::create_swapchain_resources()
{
    VkDevice device = context_.device();
    const auto image_count = swapchain_.images().size();

    render_finished_.resize(image_count, VK_NULL_HANDLE);
    VkSemaphoreCreateInfo semaphore_info{};
    semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    for (auto& semaphore : render_finished_) {
        if (vkCreateSemaphore(device, &semaphore_info, nullptr, &semaphore) != VK_SUCCESS) {
            return std::unexpected(make_error("failed to create per-image present semaphore"));
        }
    }

    command_buffers_.resize(image_count, VK_NULL_HANDLE);
    VkCommandBufferAllocateInfo allocate_info{};
    allocate_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocate_info.commandPool = context_.command_pool();
    allocate_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocate_info.commandBufferCount = static_cast<std::uint32_t>(image_count);
    if (vkAllocateCommandBuffers(device, &allocate_info, command_buffers_.data()) != VK_SUCCESS) {
        return std::unexpected(make_error("failed to allocate viewport command buffers"));
    }
    return {};
}

void ViewportSession::destroy_swapchain_resources()
{
    VkDevice device = context_.device();
    if (device == VK_NULL_HANDLE) {
        return;
    }
    if (!command_buffers_.empty()) {
        vkFreeCommandBuffers(device, context_.command_pool(), static_cast<std::uint32_t>(command_buffers_.size()),
                             command_buffers_.data());
        command_buffers_.clear();
    }
    for (VkSemaphore semaphore : render_finished_) {
        if (semaphore != VK_NULL_HANDLE) {
            vkDestroySemaphore(device, semaphore, nullptr);
        }
    }
    render_finished_.clear();
}

Result<void> ViewportSession::recreate_swapchain()
{
    vkDeviceWaitIdle(context_.device());
    destroy_swapchain_resources();

    int width = static_cast<int>(swapchain_.extent().width);
    int height = static_cast<int>(swapchain_.extent().height);
    if (auto handle = connection_.surface_handle(x_window_); handle) {
        width = handle->width;
        height = handle->height;
    }

    auto swapchain = VulkanSwapchain::create(context_, width, height);
    if (!swapchain) {
        return std::unexpected(swapchain.error());
    }
    swapchain_ = std::move(*swapchain);

    info_.width = static_cast<int>(swapchain_.extent().width);
    info_.height = static_cast<int>(swapchain_.extent().height);
    return create_swapchain_resources();
}

void ViewportSession::render_loop()
{
    VkDevice device = context_.device();
    VkQueue queue = context_.graphics_queue();
    int frame = 0;
    std::uint64_t counter = 0;

    while (running_) {
        vkWaitForFences(device, 1, &in_flight_[frame], VK_TRUE, UINT64_MAX);

        std::uint32_t image_index = 0;
        VkResult acquired = vkAcquireNextImageKHR(device, swapchain_.get(), UINT64_MAX, image_available_[frame],
                                                  VK_NULL_HANDLE, &image_index);
        if (acquired == VK_ERROR_OUT_OF_DATE_KHR) {
            if (!recreate_swapchain()) {
                break;
            }
            continue;
        }
        if (acquired != VK_SUCCESS && acquired != VK_SUBOPTIMAL_KHR) {
            break;
        }

        vkResetFences(device, 1, &in_flight_[frame]);

        VkCommandBuffer command_buffer = command_buffers_[image_index];
        VkImage image = swapchain_.images()[image_index];
        vkResetCommandBuffer(command_buffer, 0);

        VkCommandBufferBeginInfo begin_info{};
        begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(command_buffer, &begin_info);

        transition(command_buffer, image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0,
                   VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

        const VkClearColorValue color = test_pattern_color(counter);
        const VkImageSubresourceRange range = whole_color_image();
        vkCmdClearColorImage(command_buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &color, 1, &range);

        transition(command_buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                   VK_ACCESS_TRANSFER_WRITE_BIT, 0, VK_PIPELINE_STAGE_TRANSFER_BIT,
                   VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);

        vkEndCommandBuffer(command_buffer);

        const VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        VkSubmitInfo submit{};
        submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.waitSemaphoreCount = 1;
        submit.pWaitSemaphores = &image_available_[frame];
        submit.pWaitDstStageMask = &wait_stage;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &command_buffer;
        submit.signalSemaphoreCount = 1;
        submit.pSignalSemaphores = &render_finished_[image_index];
        if (vkQueueSubmit(queue, 1, &submit, in_flight_[frame]) != VK_SUCCESS) {
            break;
        }

        VkPresentInfoKHR present{};
        present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        present.waitSemaphoreCount = 1;
        present.pWaitSemaphores = &render_finished_[image_index];
        present.swapchainCount = 1;
        VkSwapchainKHR swapchain = swapchain_.get();
        present.pSwapchains = &swapchain;
        present.pImageIndices = &image_index;

        VkResult presented = vkQueuePresentKHR(queue, &present);
        if (presented == VK_ERROR_OUT_OF_DATE_KHR || presented == VK_SUBOPTIMAL_KHR) {
            if (!recreate_swapchain()) {
                break;
            }
        }

        frame = (frame + 1) % kFramesInFlight;
        ++counter;
        std::this_thread::sleep_for(std::chrono::milliseconds(8));
    }

    vkDeviceWaitIdle(device);
}

} // namespace toi::viewport
