#include "toi/viewport/viewport_session.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <utility>

#ifdef TOI_ENABLE_OVRTX
#include "toi/render/viewport_guides.hpp"

#include <X11/Xlib.h>

#include <vector>
#endif

namespace toi::viewport {
namespace {

[[nodiscard]] VkImageSubresourceRange whole_color_image()
{
    return VkImageSubresourceRange{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
}

[[nodiscard]] Result<void> require_vk(VkResult result, std::string_view operation)
{
    if (result == VK_SUCCESS) {
        return {};
    }
    return std::unexpected(make_error(std::string(operation) + " failed: VkResult " + std::to_string(result)));
}

[[nodiscard]] VkRect2D contained_rect(int source_width, int source_height, VkExtent2D destination)
{
    const std::int64_t source_w = std::max(1, source_width);
    const std::int64_t source_h = std::max(1, source_height);
    const std::int64_t destination_w = std::max<std::uint32_t>(1, destination.width);
    const std::int64_t destination_h = std::max<std::uint32_t>(1, destination.height);

    std::int64_t width = destination_w;
    std::int64_t height = destination_h;
    if (destination_w * source_h > destination_h * source_w) {
        width = std::max<std::int64_t>(1, (destination_h * source_w + source_h / 2) / source_h);
    } else {
        height = std::max<std::int64_t>(1, (destination_w * source_h + source_w / 2) / source_w);
    }
    return VkRect2D{
        .offset = {static_cast<std::int32_t>((destination_w - width) / 2),
                   static_cast<std::int32_t>((destination_h - height) / 2)},
        .extent = {static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height)},
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

#ifdef TOI_ENABLE_OVRTX
[[nodiscard]] OverlayCamera to_overlay_camera(const render::GrowthPreviewCamera& camera)
{
    return OverlayCamera{
        .eye = {camera.eye.x, camera.eye.y, camera.eye.z},
        .right = {camera.right.x, camera.right.y, camera.right.z},
        .up = {camera.up.x, camera.up.y, camera.up.z},
        .negative_forward = {camera.negative_forward.x, camera.negative_forward.y, camera.negative_forward.z},
        .focal_length = static_cast<float>(camera.focal_length),
        .horizontal_aperture = static_cast<float>(camera.horizontal_aperture),
        .vertical_aperture = static_cast<float>(camera.vertical_aperture),
        .near_clip = camera.near_clip,
        .far_clip = camera.far_clip,
    };
}

[[nodiscard]] std::vector<OverlayLine> build_overlay_lines(const render::GrowthPreviewCamera& camera)
{
    const auto overlay = render::make_growth_preview_guides(camera);
    std::vector<OverlayLine> lines;
    lines.reserve(overlay.lines.size());
    for (const auto& line : overlay.lines) {
        lines.push_back(OverlayLine{
            .start = {line.start.x, line.start.y, line.start.z},
            .end = {line.end.x, line.end.y, line.end.z},
            .color = {line.color.x, line.color.y, line.color.z},
            .alpha = line.alpha,
        });
    }
    return lines;
}

// Bias the depth test by a fraction of the focus distance so guide lines lying
// on a surface are not erased by their own z-fighting, while still being
// occluded by clearly nearer geometry.
[[nodiscard]] float overlay_depth_bias(const render::GrowthPreviewCamera& camera)
{
    const float dx = camera.eye.x - camera.target.x;
    const float dy = camera.eye.y - camera.target.y;
    const float dz = camera.eye.z - camera.target.z;
    const float focus_distance = std::sqrt(dx * dx + dy * dy + dz * dz);
    return std::max(0.01F, focus_distance * 0.0005F);
}
#endif

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

    auto session = std::unique_ptr<ViewportSession>(new ViewportSession());
    session->connection_ = std::move(*connection);
    session->context_ = std::move(*context);

    // VulkanSwapchain retains its owning context address, so construct it only
    // after the context has reached its stable lifetime inside the session.
    auto swapchain = VulkanSwapchain::create(session->context_, initial_width, initial_height);
    if (!swapchain) {
        return std::unexpected(swapchain.error());
    }
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
    if (auto idle = require_vk(vkDeviceWaitIdle(context_.device()), "vkDeviceWaitIdle"); !idle) {
        return std::unexpected(idle.error());
    }

    int width = static_cast<int>(swapchain_.extent().width);
    int height = static_cast<int>(swapchain_.extent().height);
    if (auto handle = connection_.surface_handle(x_window_); handle) {
        width = handle->width;
        height = handle->height;
    }

#ifdef TOI_ENABLE_OVRTX
    // Framebuffers and image views must die before their swapchain images.
    overlay_.reset_swapchain();
#endif
    destroy_swapchain_resources();
    swapchain_.reset();
    auto replacement = VulkanSwapchain::create(context_, width, height);
    if (!replacement) {
        return std::unexpected(replacement.error());
    }
    swapchain_ = std::move(*replacement);

    info_.width = static_cast<int>(swapchain_.extent().width);
    info_.height = static_cast<int>(swapchain_.extent().height);
    std::fprintf(stdout, "native viewport swapchain resized: %dx%d\n", info_.width, info_.height);
    std::fflush(stdout);
    if (auto resources = create_swapchain_resources(); !resources) {
        return std::unexpected(resources.error());
    }
#ifdef TOI_ENABLE_OVRTX
    if (overlay_ready_) {
        if (auto targets = overlay_.set_swapchain(context_, swapchain_); !targets) {
            return std::unexpected(targets.error());
        }
    }
#endif
    return {};
}

void ViewportSession::record_test_pattern(VkCommandBuffer command_buffer, VkImage image, std::uint64_t frame)
{
    transition(command_buffer, image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0,
               VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

    const VkClearColorValue color = test_pattern_color(frame);
    const VkImageSubresourceRange range = whole_color_image();
    vkCmdClearColorImage(command_buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &color, 1, &range);

    transition(command_buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
               VK_ACCESS_TRANSFER_WRITE_BIT, 0, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
}

void ViewportSession::render_loop()
{
    VkDevice device = context_.device();
    VkQueue queue = context_.graphics_queue();
    int frame = 0;
    std::uint64_t counter = 0;
    std::uint64_t idle_skip_ticks = 0;
    // ~1s at the 8ms idle poll interval: a slow keepalive present so the viewport
    // self-corrects from any display change the dirty/resize checks miss.
    constexpr std::uint64_t kIdleKeepaliveTicks = 125;

    while (running_) {
#ifdef TOI_ENABLE_OVRTX
        poll_pointer();
        // Swap the double-buffered slots as soon as a kicked frame's copies land.
        complete_pending_produce();
        // A settled preview does not need to re-present the same frame: keep
        // polling input every tick for responsiveness, but skip the whole GPU
        // present until something changes (new stage, camera move, guide toggle,
        // resize) or a produced frame completes. This takes idle GPU/compositor
        // load to near zero. Startup and the animated test pattern (before the
        // first growth frame) always present.
        if (growth_ready_ && slots_[present_slot_].timeline_value != 0 && !needs_present_.exchange(false) &&
            ++idle_skip_ticks < kIdleKeepaliveTicks) {
            // Poll faster while a produce is in flight so its completion is
            // presented promptly; back off when fully idle.
            std::this_thread::sleep_for(std::chrono::milliseconds(produce_pending_ ? 2 : 8));
            continue;
        }
        idle_skip_ticks = 0;
#endif
        if (auto waited = require_vk(vkWaitForFences(device, 1, &in_flight_[frame], VK_TRUE, UINT64_MAX),
                                     "vkWaitForFences");
            !waited) {
            std::fprintf(stderr, "native viewport failed: %s\n", waited.error().message.c_str());
            break;
        }

#ifdef TOI_ENABLE_OVRTX
        // Kick the next ovrtx render + copies after the fence wait: with one
        // Vulkan frame in flight no submit still reads the produce slot, so CUDA
        // may overwrite it while this iteration presents the other slot.
        if (ensure_growth_renderer()) {
            (void)produce_growth_frame();
        }
#endif

        std::uint32_t image_index = 0;
        const VkResult acquired = vkAcquireNextImageKHR(device, swapchain_.get(), UINT64_MAX,
                                                        image_available_[frame], VK_NULL_HANDLE, &image_index);
        if (acquired == VK_ERROR_OUT_OF_DATE_KHR) {
            if (auto recreated = recreate_swapchain(); !recreated) {
                std::fprintf(stderr, "native viewport resize failed: %s\n", recreated.error().message.c_str());
                break;
            }
            continue;
        }
        if (acquired != VK_SUCCESS && acquired != VK_SUBOPTIMAL_KHR) {
            std::fprintf(stderr, "native viewport failed: vkAcquireNextImageKHR returned %d\n", acquired);
            break;
        }

        if (auto reset = require_vk(vkResetFences(device, 1, &in_flight_[frame]), "vkResetFences"); !reset) {
            std::fprintf(stderr, "native viewport failed: %s\n", reset.error().message.c_str());
            break;
        }

        VkCommandBuffer command_buffer = command_buffers_[image_index];
        VkImage image = swapchain_.images()[image_index];
        if (auto reset = require_vk(vkResetCommandBuffer(command_buffer, 0), "vkResetCommandBuffer"); !reset) {
            std::fprintf(stderr, "native viewport failed: %s\n", reset.error().message.c_str());
            break;
        }

        VkCommandBufferBeginInfo begin_info{};
        begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        if (auto begun = require_vk(vkBeginCommandBuffer(command_buffer, &begin_info), "vkBeginCommandBuffer");
            !begun) {
            std::fprintf(stderr, "native viewport failed: %s\n", begun.error().message.c_str());
            break;
        }

#ifdef TOI_ENABLE_OVRTX
        const bool drew_growth = record_growth_present(command_buffer, image_index);
#else
        const bool drew_growth = false;
#endif
        if (!drew_growth) {
            record_test_pattern(command_buffer, image, counter);
        }

        if (auto ended = require_vk(vkEndCommandBuffer(command_buffer), "vkEndCommandBuffer"); !ended) {
            std::fprintf(stderr, "native viewport failed: %s\n", ended.error().message.c_str());
            break;
        }

        VkSemaphore wait_semaphores[2] = {image_available_[frame], VK_NULL_HANDLE};
        // Acquisition must complete before ANY access to the swapchain image: the
        // first thing the command buffer does is transition it (a write) from its
        // just-acquired state, and that write must be ordered after the presentation
        // engine's prior read. Waiting the acquire semaphore at ALL_COMMANDS gives
        // that execution dependency (a TRANSFER-only wait lets the TOP_OF_PIPE
        // layout transition race the acquire — SYNC-HAZARD-WRITE-AFTER-READ).
        // The CUDA copies feed both the blit (TRANSFER) and the overlay's distance
        // sampling (FRAGMENT_SHADER), so that wait must cover both stages.
        VkPipelineStageFlags wait_stages[2] = {
            VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT};
        std::uint32_t wait_count = 1;
#ifdef TOI_ENABLE_OVRTX
        // Reading the present slot waits for its CUDA copies via the timeline
        // value; timeline waits are non-consuming, so re-presenting the same slot
        // re-waits the same (already signaled) value.
        std::uint64_t wait_values[2] = {0, 0};
        if (drew_growth) {
            wait_semaphores[1] = frames_ready_.vk_semaphore();
            wait_values[1] = slots_[present_slot_].timeline_value;
            wait_count = 2;
        }
        VkTimelineSemaphoreSubmitInfo timeline_info{};
        timeline_info.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
        timeline_info.waitSemaphoreValueCount = wait_count;
        timeline_info.pWaitSemaphoreValues = wait_values;
#endif
        VkSubmitInfo submit{};
        submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
#ifdef TOI_ENABLE_OVRTX
        submit.pNext = &timeline_info;
#endif
        submit.waitSemaphoreCount = wait_count;
        submit.pWaitSemaphores = wait_semaphores;
        submit.pWaitDstStageMask = wait_stages;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &command_buffer;
        submit.signalSemaphoreCount = 1;
        submit.pSignalSemaphores = &render_finished_[image_index];
        if (auto submitted = require_vk(vkQueueSubmit(queue, 1, &submit, in_flight_[frame]), "vkQueueSubmit");
            !submitted) {
            std::fprintf(stderr, "native viewport failed: %s\n", submitted.error().message.c_str());
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

        const VkResult presented = vkQueuePresentKHR(queue, &present);
        if (presented != VK_SUCCESS && presented != VK_ERROR_OUT_OF_DATE_KHR && presented != VK_SUBOPTIMAL_KHR) {
            std::fprintf(stderr, "native viewport failed: vkQueuePresentKHR returned %d\n", presented);
            break;
        }
        if (acquired == VK_SUBOPTIMAL_KHR || presented == VK_ERROR_OUT_OF_DATE_KHR ||
            presented == VK_SUBOPTIMAL_KHR) {
            if (auto recreated = recreate_swapchain(); !recreated) {
                std::fprintf(stderr, "native viewport resize failed: %s\n", recreated.error().message.c_str());
                break;
            }
        }

        frame = (frame + 1) % kFramesInFlight;
        ++counter;
#ifdef TOI_ENABLE_OVRTX
        // Pace the loop: the test pattern runs at ~125Hz, and while a produce is
        // cooking, re-presents (drag feedback) and completion polls run at ~500Hz.
        // A completed swap loops straight through to kick the next render.
        if (!drew_growth) {
            std::this_thread::sleep_for(std::chrono::milliseconds(8));
        } else if (produce_pending_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
#else
        std::this_thread::sleep_for(std::chrono::milliseconds(8));
#endif
    }

    vkDeviceWaitIdle(device);
#ifdef TOI_ENABLE_OVRTX
    // Release CUDA/ovrtx on the thread that created them, after the device is
    // idle and any in-flight copies into the shared images have drained.
    if (produce_pending_ && copy_done_ != nullptr) {
        cudaEventSynchronize(copy_done_);
        produce_pending_ = false;
    }
    overlay_.reset();
    overlay_ready_ = false;
    for (auto& slot : slots_) {
        slot.distance.reset();
        slot.color.reset();
    }
    frames_ready_.reset();
    if (copy_done_ != nullptr) {
        cudaEventDestroy(copy_done_);
        copy_done_ = nullptr;
    }
    renderer_.reset();
#endif
}

#ifdef TOI_ENABLE_OVRTX

void ViewportSession::set_pending_stage(render::GrowthPreviewStageProjection stage)
{
    std::lock_guard<std::mutex> lock(stage_mutex_);
    pending_stage_ = std::move(stage);
    stage_dirty_ = true;
    needs_present_ = true;
}

void ViewportSession::set_guide_options(bool guides_visible, bool world_origin_axes_visible)
{
    guides_visible_ = guides_visible;
    world_origin_axes_visible_ = world_origin_axes_visible;
    needs_present_ = true;
}

// Poll the pointer against the viewport window: left drag orbits, right drag
// dollies. Polling (not events) sidesteps the shell's input routing.
void ViewportSession::poll_pointer()
{
    auto* display = static_cast<Display*>(connection_.display());
    if (display == nullptr) {
        return;
    }
    Window root = 0;
    Window child = 0;
    int root_x = 0;
    int root_y = 0;
    int win_x = 0;
    int win_y = 0;
    unsigned int mask = 0;
    if (XQueryPointer(display, static_cast<Window>(x_window_), &root, &child, &root_x, &root_y, &win_x, &win_y,
                      &mask) == 0) {
        last_dragging_ = false;
        return;
    }

    const bool left = (mask & Button1Mask) != 0;
    const bool right = (mask & Button3Mask) != 0;
    const int dx = win_x - last_pointer_x_;
    const int dy = win_y - last_pointer_y_;
    if ((left || right) && last_dragging_ && (dx != 0 || dy != 0)) {
        std::lock_guard<std::mutex> lock(camera_mutex_);
        if (!orbit_initialized_ && has_base_camera_) {
            orbit_ = render::orbit_view_from_camera(base_camera_);
            orbit_initialized_ = true;
        }
        if (orbit_initialized_) {
            if (left) {
                orbit_ = render::rotate_orbit_view(orbit_, static_cast<float>(-dx) * 0.006F,
                                                   static_cast<float>(dy) * 0.006F);
            } else {
                orbit_ = render::dolly_orbit_view(orbit_, 1.0F + static_cast<float>(dy) * 0.004F);
            }
            orbit_dirty_ = true;
            needs_present_ = true;
        }
    }
    last_dragging_ = left || right;
    last_pointer_x_ = win_x;
    last_pointer_y_ = win_y;

    // Detect a resize while the preview is idle: if the window no longer matches
    // the swapchain, force a present so the loop acquires, hits OUT_OF_DATE, and
    // recreates the swapchain. Without this the present gate would skip resizes.
    Window geo_root = 0;
    int geo_x = 0;
    int geo_y = 0;
    unsigned int geo_w = 0;
    unsigned int geo_h = 0;
    unsigned int geo_border = 0;
    unsigned int geo_depth = 0;
    if (XGetGeometry(display, static_cast<Drawable>(x_window_), &geo_root, &geo_x, &geo_y, &geo_w, &geo_h, &geo_border,
                     &geo_depth) != 0 &&
        (geo_w != swapchain_.extent().width || geo_h != swapchain_.extent().height)) {
        needs_present_ = true;
    }
}

bool ViewportSession::ensure_growth_renderer()
{
    if (growth_ready_) {
        return true;
    }
    if (growth_failed_) {
        return false;
    }

    render::GrowthPreviewStageProjection stage;
    {
        std::lock_guard<std::mutex> lock(stage_mutex_);
        if (!pending_stage_) {
            return false;
        }
        stage = *pending_stage_;
    }

    if (auto selected = select_cuda_device(0); !selected) {
        growth_failed_ = true;
        return false;
    }

    auto renderer = ovrtx::RendererSession::create({.asset_search_path = stage.usd_stage.asset_search_path});
    if (!renderer) {
        growth_failed_ = true;
        return false;
    }
    renderer_ = std::make_unique<ovrtx::RendererSession>(std::move(*renderer));

    auto fail = [this] {
        for (auto& slot : slots_) {
            slot.color.reset();
            slot.distance.reset();
        }
        overlay_.reset();
        overlay_ready_ = false;
        frames_ready_.reset();
        renderer_.reset();
        growth_failed_ = true;
        return false;
    };

    for (auto& slot : slots_) {
        auto color = CudaInteropImage::create(context_, stage.usd_stage.width, stage.usd_stage.height);
        if (!color) {
            return fail();
        }
        slot.color = std::move(*color);
    }

    auto frames_ready = CudaInteropTimelineSemaphore::create(context_);
    if (!frames_ready) {
        return fail();
    }
    frames_ready_ = std::move(*frames_ready);

    if (cudaEventCreateWithFlags(&copy_done_, cudaEventDisableTiming) != cudaSuccess) {
        copy_done_ = nullptr;
        return fail();
    }

    // Guide overlay (world-origin axes) drawn over the presented frame, with a
    // shared scene-distance image (ovrtx DistanceToCameraSD) per slot so the
    // axes are occluded by the plant/ground geometry of the frame on screen.
    auto distance_a = CudaInteropFloatImage::create(context_, stage.usd_stage.width, stage.usd_stage.height);
    auto distance_b = CudaInteropFloatImage::create(context_, stage.usd_stage.width, stage.usd_stage.height);
    auto overlay = ViewportOverlay::create(context_, swapchain_.format());
    if (distance_a && distance_b && overlay) {
        slots_[0].distance = std::move(*distance_a);
        slots_[1].distance = std::move(*distance_b);
        overlay_ = std::move(*overlay);
        overlay_ready_ = overlay_.set_swapchain(context_, swapchain_).has_value() &&
                         overlay_.set_scene_distance(0, slots_[0].distance.view()).has_value() &&
                         overlay_.set_scene_distance(1, slots_[1].distance.view()).has_value();
        if (!overlay_ready_) {
            overlay_.reset();
            slots_[0].distance.reset();
            slots_[1].distance.reset();
        }
    }

    growth_ready_ = true;
    return true;
}

Result<void> ViewportSession::resize_growth_slot(int slot_index, int width, int height)
{
    if (produce_pending_) {
        return std::unexpected(make_error("cannot resize a Growth preview slot while a frame copy is pending"));
    }

    auto color = CudaInteropImage::create(context_, width, height);
    if (!color) {
        return std::unexpected(color.error());
    }
    CudaInteropFloatImage distance;
    if (overlay_ready_) {
        auto created = CudaInteropFloatImage::create(context_, width, height);
        if (!created) {
            return std::unexpected(created.error());
        }
        distance = std::move(*created);
    }

    if (auto idle = require_vk(vkDeviceWaitIdle(context_.device()), "vkDeviceWaitIdle"); !idle) {
        return std::unexpected(idle.error());
    }
    if (overlay_ready_) {
        if (auto wired = overlay_.set_scene_distance(static_cast<std::uint32_t>(slot_index), distance.view()); !wired) {
            return std::unexpected(wired.error());
        }
    }

    GrowthSlot& slot = slots_[slot_index];
    slot.color = std::move(*color);
    slot.distance = std::move(distance);
    slot.timeline_value = 0;
    slot.has_distance = false;
    needs_present_ = true;
    return {};
}

bool ViewportSession::produce_growth_frame()
{
    if (produce_pending_) {
        return false;
    }

    std::optional<render::GrowthPreviewStageProjection> stage_to_submit;
    {
        std::lock_guard<std::mutex> lock(stage_mutex_);
        if (stage_dirty_ && pending_stage_) {
            stage_to_submit = *pending_stage_;
            stage_dirty_ = false;
        }
    }
    const bool submitted_new = stage_to_submit.has_value();

    // Apply the interactive orbit camera on top of the stage's default framing.
    bool apply_camera = submitted_new;
    render::GrowthPreviewCamera oriented_camera;
    {
        std::lock_guard<std::mutex> lock(camera_mutex_);
        if (submitted_new) {
            base_camera_ = stage_to_submit->camera;
            has_base_camera_ = true;
        }
        if (has_base_camera_) {
            if (!orbit_initialized_) {
                orbit_ = render::orbit_view_from_camera(base_camera_);
                orbit_initialized_ = true;
                apply_camera = true;
            }
            if (orbit_dirty_) {
                orbit_dirty_ = false;
                apply_camera = true;
            }
            if (apply_camera) {
                oriented_camera = render::apply_orbit_view(base_camera_, orbit_);
            }
        }
    }

    GrowthSlot& slot = slots_[produce_slot_];
    // Sample the scene distance for depth-aware guide occlusion only when the
    // guides are actually drawn; a color-only frame skips the extra output.
    const bool want_distance = overlay_ready_ && guides_visible_ && world_origin_axes_visible_ &&
                               slot.distance.view() != VK_NULL_HANDLE;

    // Re-render ovrtx only when something changed; idle frames re-present the
    // present slot. The path tracer flickers (and pegs the GPU) if it is stepped
    // continuously on an unchanging scene.
    const bool rendered_once = slots_[present_slot_].timeline_value != 0 || slot.timeline_value != 0;
    if (!submitted_new && !apply_camera && rendered_once && want_distance == last_draw_guides_) {
        return false;
    }

    if (stage_to_submit) {
        if (auto submitted = renderer_->submit_growth_preview(*stage_to_submit); !submitted) {
            return false;
        }
    }
    if (apply_camera && has_base_camera_) {
        if (auto set = renderer_->set_growth_preview_camera(oriented_camera); !set) {
            return false;
        }
    }

    auto rendered = renderer_->render_cuda_frame(want_distance ? ovrtx::RenderFrameOutputs::ColorAndDistance
                                                               : ovrtx::RenderFrameOutputs::Color);
    if (!rendered) {
        return false;
    }
    if (rendered->width != slot.color.width() || rendered->height != slot.color.height()) {
        if (auto resized = resize_growth_slot(produce_slot_, rendered->width, rendered->height); !resized) {
            std::fprintf(stderr, "native viewport render resize failed: %s\n", resized.error().message.c_str());
            return false;
        }
    }

    // The copies are stream-ordered after ovrtx's render completion. copy_done_
    // lets the CPU detect completion (to swap the slots); the timeline value
    // lets the Vulkan submit order its blit/sampling after the copies.
    if (auto copied = slot.color.copy_from_cuda_array(rendered->color_cuda_array, rendered->width, rendered->height,
                                                      rendered->sync_stream);
        !copied) {
        return false;
    }
    slot.has_distance = false;
    if (want_distance && rendered->scene_distance_cuda_array != nullptr) {
        if (auto copied = slot.distance.copy_from_cuda_array(rendered->scene_distance_cuda_array, rendered->width,
                                                             rendered->height, rendered->sync_stream);
            !copied) {
            return false;
        }
        slot.has_distance = true;
    }
    slot.camera = rendered->camera;

    if (cudaEventRecord(copy_done_, reinterpret_cast<cudaStream_t>(rendered->sync_stream)) != cudaSuccess) {
        return false;
    }
    if (auto signaled = frames_ready_.signal_from_ovrtx_stream(timeline_value_ + 1, rendered->sync_stream);
        !signaled) {
        return false;
    }
    ++timeline_value_;
    slot.timeline_value = timeline_value_;
    last_draw_guides_ = want_distance;
    produce_pending_ = true;
    return true;
}

void ViewportSession::complete_pending_produce()
{
    if (!produce_pending_) {
        return;
    }
    const auto query = cudaEventQuery(copy_done_);
    if (query == cudaErrorNotReady) {
        return;
    }
    produce_pending_ = false;
    if (query != cudaSuccess) {
        return; // Keep presenting the old slot; the next change retries.
    }
    std::swap(present_slot_, produce_slot_);
    const GrowthSlot& present = slots_[present_slot_];
    const GrowthSlot& standby = slots_[produce_slot_];
    if (standby.color.width() != present.color.width() || standby.color.height() != present.color.height()) {
        if (auto resized = resize_growth_slot(produce_slot_, present.color.width(), present.color.height()); !resized) {
            std::fprintf(stderr, "native viewport standby resize failed: %s\n", resized.error().message.c_str());
        } else {
            std::fprintf(stdout, "Growth preview render resized: %dx%d\n", present.color.width(),
                         present.color.height());
            std::fflush(stdout);
        }
    }
    needs_present_ = true;
}

bool ViewportSession::record_growth_present(VkCommandBuffer command_buffer, std::uint32_t image_index)
{
    GrowthSlot& slot = slots_[present_slot_];
    if (!growth_ready_ || slot.timeline_value == 0) {
        return false;
    }
    VkImage swapchain_image = swapchain_.images()[image_index];

    // Move the slot's color image to a transfer-read layout for the blit.
    transition(command_buffer, slot.color.image(), VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
               VK_ACCESS_MEMORY_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
               VK_PIPELINE_STAGE_TRANSFER_BIT);
    transition(command_buffer, swapchain_image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0,
               VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

    const VkClearColorValue black{{0.0F, 0.0F, 0.0F, 1.0F}};
    const VkImageSubresourceRange range = whole_color_image();
    vkCmdClearColorImage(command_buffer, swapchain_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &black, 1, &range);

    const VkRect2D content = contained_rect(slot.color.width(), slot.color.height(), swapchain_.extent());
    VkImageBlit blit{};
    blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    blit.srcOffsets[1] = {slot.color.width(), slot.color.height(), 1};
    blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    blit.dstOffsets[0] = {content.offset.x, content.offset.y, 0};
    blit.dstOffsets[1] = {content.offset.x + static_cast<std::int32_t>(content.extent.width),
                          content.offset.y + static_cast<std::int32_t>(content.extent.height), 1};
    vkCmdBlitImage(command_buffer, slot.color.image(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, swapchain_image,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);

    // Return the slot image to GENERAL for its next CUDA write.
    transition(command_buffer, slot.color.image(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,
               VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_MEMORY_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
               VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);

    // Draw guide lines over the frame using the slot's own camera and distance
    // image, so occlusion matches the frame on screen; the overlay render pass
    // also moves the swapchain image to PRESENT_SRC. Otherwise transition it
    // directly.
    if (overlay_ready_ && guides_visible_ && world_origin_axes_visible_ && slot.has_distance) {
        const auto lines = build_overlay_lines(slot.camera);
        const auto overlay_camera = to_overlay_camera(slot.camera);
        const float depth_bias = overlay_depth_bias(slot.camera);
        if (overlay_.record(command_buffer, image_index, swapchain_.extent(), content, overlay_camera, lines,
                            depth_bias, static_cast<std::uint32_t>(present_slot_))) {
            return true;
        }
    }
    transition(command_buffer, swapchain_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
               VK_ACCESS_TRANSFER_WRITE_BIT, 0, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
    return true;
}

#endif // TOI_ENABLE_OVRTX

} // namespace toi::viewport
