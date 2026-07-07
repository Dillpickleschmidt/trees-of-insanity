#include "toi/viewport/viewport_session.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
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
    if (auto resources = create_swapchain_resources(); !resources) {
        return std::unexpected(resources.error());
    }
#ifdef TOI_ENABLE_OVRTX
    if (growth_ready_) {
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
        // A settled preview does not need to re-present the same frame: keep
        // polling input every tick for responsiveness, but skip the whole GPU
        // present until something changes (new stage, camera move, guide toggle,
        // resize). This takes idle GPU/compositor load to near zero. Startup and
        // the animated test pattern (before the first growth frame) always present.
        if (growth_ready_ && rendered_once_ && !needs_present_.exchange(false) &&
            ++idle_skip_ticks < kIdleKeepaliveTicks) {
            std::this_thread::sleep_for(std::chrono::milliseconds(8));
            continue;
        }
        idle_skip_ticks = 0;
#endif
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

#ifdef TOI_ENABLE_OVRTX
        const bool drew_growth = record_growth_frame(command_buffer, image_index);
#else
        const bool drew_growth = false;
#endif
        if (!drew_growth) {
            record_test_pattern(command_buffer, image, counter);
        }

        vkEndCommandBuffer(command_buffer);

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
        // Only wait on the CUDA semaphore when this frame actually re-rendered;
        // idle frames re-blit an interop image CUDA finished writing earlier.
        if (drew_growth && cuda_signaled_this_frame_) {
            wait_semaphores[1] = cuda_done_.vk_semaphore();
            wait_count = 2;
        }
#endif
        VkSubmitInfo submit{};
        submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.waitSemaphoreCount = wait_count;
        submit.pWaitSemaphores = wait_semaphores;
        submit.pWaitDstStageMask = wait_stages;
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
        // Cap the idle rate: the test pattern and idle re-presents of the cached
        // growth frame don't need to run flat out; an active re-render does.
#ifdef TOI_ENABLE_OVRTX
        const bool cap_idle_rate = !drew_growth || !cuda_signaled_this_frame_;
#else
        const bool cap_idle_rate = true; // no growth path: always the animated test pattern
#endif
        if (cap_idle_rate) {
            std::this_thread::sleep_for(std::chrono::milliseconds(8));
        }
    }

#ifdef TOI_ENABLE_OVRTX
    // Release CUDA/ovrtx on the thread that created them.
    overlay_.reset();
    scene_distance_.reset();
    cuda_done_.reset();
    interop_.reset();
    renderer_.reset();
#endif
    vkDeviceWaitIdle(device);
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

    auto interop = CudaInteropImage::create(context_, stage.usd_stage.width, stage.usd_stage.height);
    if (!interop) {
        renderer_.reset();
        growth_failed_ = true;
        return false;
    }
    interop_ = std::move(*interop);

    auto cuda_done = CudaInteropSemaphore::create(context_);
    if (!cuda_done) {
        interop_.reset();
        renderer_.reset();
        growth_failed_ = true;
        return false;
    }
    cuda_done_ = std::move(*cuda_done);

    // Guide overlay (world-origin axes) drawn over the presented frame, with a
    // shared scene-distance image (ovrtx DistanceToCameraSD) so the axes are
    // occluded by the plant/ground geometry.
    auto scene_distance = CudaInteropFloatImage::create(context_, stage.usd_stage.width, stage.usd_stage.height);
    auto overlay = ViewportOverlay::create(context_, swapchain_.format());
    if (scene_distance && overlay) {
        scene_distance_ = std::move(*scene_distance);
        overlay_ = std::move(*overlay);
        if (auto targets = overlay_.set_swapchain(context_, swapchain_); !targets) {
            overlay_.reset();
            scene_distance_.reset();
        } else if (auto bound = overlay_.set_scene_distance(scene_distance_.view()); !bound) {
            overlay_.reset();
            scene_distance_.reset();
        }
    }

    growth_ready_ = true;
    return true;
}

bool ViewportSession::record_growth_frame(VkCommandBuffer command_buffer, std::uint32_t image_index)
{
    cuda_signaled_this_frame_ = false;
    if (!ensure_growth_renderer()) {
        return false;
    }
    VkImage swapchain_image = swapchain_.images()[image_index];

    std::optional<render::GrowthPreviewStageProjection> stage_to_submit;
    {
        std::lock_guard<std::mutex> lock(stage_mutex_);
        if (stage_dirty_ && pending_stage_) {
            stage_to_submit = *pending_stage_;
            stage_dirty_ = false;
        }
    }
    bool submitted_new = false;
    if (stage_to_submit) {
        if (auto submitted = renderer_->submit_growth_preview(*stage_to_submit); !submitted) {
            return false;
        }
        submitted_new = true;
    }

    // Apply the interactive orbit camera on top of the stage's default framing.
    bool apply_camera = submitted_new;
    render::GrowthPreviewCamera oriented_camera;
    {
        std::lock_guard<std::mutex> lock(camera_mutex_);
        if (submitted_new && stage_to_submit) {
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
    if (apply_camera && has_base_camera_) {
        if (auto set = renderer_->set_growth_preview_camera(oriented_camera); !set) {
            return false;
        }
    }

    // Sample the scene distance for depth-aware guide occlusion only when the
    // guides are actually drawn; a color-only frame skips the extra output.
    const bool draw_guides =
        guides_visible_ && world_origin_axes_visible_ && scene_distance_.view() != VK_NULL_HANDLE;

    // Re-render ovrtx only when something changed; idle frames re-present the
    // cached interop image. The path tracer flickers (and pegs the GPU) if it is
    // stepped continuously on an unchanging scene.
    const bool needs_render = submitted_new || apply_camera || !rendered_once_ || draw_guides != last_draw_guides_;
    if (needs_render) {
        auto rendered = renderer_->render_cuda_frame(draw_guides ? ovrtx::RenderFrameOutputs::ColorAndDistance
                                                                 : ovrtx::RenderFrameOutputs::Color);
        if (!rendered) {
            return false;
        }
        if (rendered->width != interop_.width() || rendered->height != interop_.height()) {
            return false;
        }

        const CudaDeviceFrameView view{
            .device_data = rendered->tensor->data,
            .width = rendered->width,
            .height = rendered->height,
            .channel_count = rendered->channel_count,
            .row_stride_bytes = rendered->row_stride_bytes,
            .byte_offset = rendered->byte_offset,
            .stream = rendered->sync_stream,
        };
        if (auto copied = interop_.copy_from_cuda_frame(view); !copied) {
            return false;
        }
        if (draw_guides && rendered->scene_distance_cuda_array != nullptr) {
            if (auto copied = scene_distance_.copy_from_cuda_array(rendered->scene_distance_cuda_array, rendered->width,
                                                                  rendered->height, rendered->sync_stream);
                !copied) {
                return false;
            }
        }
        // CUDA signals when the copies complete; the Vulkan submit waits on this
        // so the blit and the overlay's distance sampling read finished data (a
        // CPU sync does not order the two GPU engines).
        if (auto signaled = cuda_done_.signal_from_ovrtx_stream(rendered->sync_stream); !signaled) {
            return false;
        }
        cuda_signaled_this_frame_ = true;
        last_camera_ = rendered->camera;
        last_draw_guides_ = draw_guides;
        rendered_once_ = true;
    }

    // Move the (freshly written or cached) interop image to a transfer-read
    // layout for the blit.
    transition(command_buffer, interop_.image(), VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
               VK_ACCESS_MEMORY_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
               VK_PIPELINE_STAGE_TRANSFER_BIT);
    transition(command_buffer, swapchain_image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0,
               VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

    VkImageBlit blit{};
    blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    blit.srcOffsets[1] = {interop_.width(), interop_.height(), 1};
    blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    blit.dstOffsets[1] = {static_cast<std::int32_t>(swapchain_.extent().width),
                          static_cast<std::int32_t>(swapchain_.extent().height), 1};
    vkCmdBlitImage(command_buffer, interop_.image(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, swapchain_image,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);

    // Return the interop image to GENERAL for the next CUDA write.
    transition(command_buffer, interop_.image(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,
               VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_MEMORY_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
               VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);

    // Draw guide lines over the frame from the last rendered camera; the overlay
    // render pass also moves the swapchain image to PRESENT_SRC. Otherwise
    // transition it directly.
    if (draw_guides && rendered_once_) {
        const auto lines = build_overlay_lines(last_camera_);
        const auto overlay_camera = to_overlay_camera(last_camera_);
        const float depth_bias = overlay_depth_bias(last_camera_);
        if (overlay_.record(command_buffer, image_index, swapchain_.extent(), overlay_camera, lines, depth_bias)) {
            return true;
        }
    }
    transition(command_buffer, swapchain_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
               VK_ACCESS_TRANSFER_WRITE_BIT, 0, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
    return true;
}

#endif // TOI_ENABLE_OVRTX

} // namespace toi::viewport
