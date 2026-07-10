#include "toi/viewport/viewport_session.hpp"

#include "toi/render/viewport_guides.hpp"
#include "toi/viewport/vulkan_presenter.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace toi::viewport {
namespace {

[[nodiscard]] VkImageSubresourceRange whole_color_image()
{
    return VkImageSubresourceRange{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
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

constexpr float kPi = 3.14159265358979323846F;
constexpr float kTwoPi = 2.0F * kPi;
constexpr float kDragDollyLogRadiusPerPixel = 0.01F;
constexpr float kWheelDollyLogRadiusPerStep = 0.15F;

[[nodiscard]] float overlay_depth_bias(const render::GrowthPreviewCamera& camera)
{
    const float dx = camera.eye.x - camera.target.x;
    const float dy = camera.eye.y - camera.target.y;
    const float dz = camera.eye.z - camera.target.z;
    const float focus_distance = std::sqrt(dx * dx + dy * dy + dz * dz);
    return std::max(0.01F, focus_distance * 0.0005F);
}

} // namespace

Result<std::unique_ptr<ViewportSession>> ViewportSession::attach(std::uintptr_t native_window, int width, int height)
{
    auto presenter = VulkanPresenter::attach(native_window, width, height, 0);
    if (!presenter) {
        return std::unexpected(presenter.error());
    }

    auto session = std::unique_ptr<ViewportSession>(new ViewportSession());
    session->presenter_ = std::move(*presenter);
    const auto extent = session->presenter_->swapchain().extent();
    session->info_ = ViewportInfo{
        .device_name = session->presenter_->context_info().physical_device_name,
        .extent = {static_cast<int>(extent.width), static_cast<int>(extent.height)},
    };
    session->status_ = ViewportStatus{
        .phase = ViewportPhase::Starting,
        .message = "Native viewport starting",
        .swapchain = session->info_.extent,
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
}

const ViewportInfo& ViewportSession::info() const
{
    return info_;
}

ViewportStatus ViewportSession::status() const
{
    std::lock_guard lock(status_mutex_);
    return status_;
}

void ViewportSession::report_error(std::string message)
{
    set_status_error(std::move(message));
}

void ViewportSession::set_pending_stage(render::GrowthPreviewStageProjection stage, ViewportCameraFraming framing)
{
    std::lock_guard lock(stage_mutex_);
    pending_stage_ = std::move(stage);
    pending_stage_framing_ = framing;
    stage_dirty_ = true;
    needs_present_ = true;
}

void ViewportSession::set_guide_options(bool guides_visible, bool world_origin_axes_visible)
{
    guides_visible_ = guides_visible;
    world_origin_axes_visible_ = world_origin_axes_visible;
    needs_present_ = true;
}

bool ViewportSession::apply_camera_input(ViewportCameraInput input)
{
    std::lock_guard lock(camera_mutex_);
    if (!base_camera_) {
        return false;
    }
    orbit_ = orbit_.value_or(render::orbit_view_from_camera(*base_camera_));

    switch (input.kind) {
    case ViewportCameraInputKind::Orbit: {
        const float radians_per_pixel = kTwoPi / static_cast<float>(std::max(1, input.viewport_height));
        orbit_ = render::rotate_orbit_view(*orbit_, -input.dx * radians_per_pixel, input.dy * radians_per_pixel);
        break;
    }
    case ViewportCameraInputKind::Pan: {
        const auto camera = render::apply_orbit_view(*base_camera_, *orbit_);
        const float world_per_pixel =
            orbit_->radius * static_cast<float>(camera.vertical_aperture / camera.focal_length) /
            static_cast<float>(std::max(1, input.viewport_height));
        const auto horizontal = growth::scale(camera.right, -input.dx * world_per_pixel);
        const auto vertical = growth::scale(camera.up, input.dy * world_per_pixel);
        orbit_->target = growth::add(orbit_->target, growth::add(horizontal, vertical));
        break;
    }
    case ViewportCameraInputKind::Dolly:
        orbit_ = render::dolly_orbit_view(*orbit_, std::exp(input.dy * kDragDollyLogRadiusPerPixel));
        break;
    case ViewportCameraInputKind::Wheel:
        orbit_ = render::dolly_orbit_view(*orbit_, std::exp(-input.dy * kWheelDollyLogRadiusPerStep));
        break;
    }

    orbit_dirty_ = true;
    needs_present_ = true;
    return true;
}

void ViewportSession::surface_changed()
{
    needs_present_ = true;
}

void ViewportSession::set_status_phase(ViewportPhase phase, std::string message)
{
    std::lock_guard lock(status_mutex_);
    status_.phase = phase;
    status_.message = std::move(message);
}

void ViewportSession::set_status_error(std::string message)
{
    set_status_phase(ViewportPhase::Error, std::move(message));
}

void ViewportSession::update_swapchain_status()
{
    const auto extent = presenter_->swapchain().extent();
    info_.extent = {static_cast<int>(extent.width), static_cast<int>(extent.height)};
    std::lock_guard lock(status_mutex_);
    status_.swapchain = info_.extent;
}

void ViewportSession::mark_frame_presented(const GrowthSlot& slot)
{
    std::lock_guard lock(status_mutex_);
    status_.phase = ViewportPhase::Ready;
    status_.message = "Native RTX viewport ready";
    status_.color = {slot.color.width(), slot.color.height()};
    status_.depth = slot.has_distance
                        ? std::optional<ViewportExtent>{{slot.distance.width(), slot.distance.height()}}
                        : std::nullopt;
    status_.frame_generation = slot.timeline_value;
}

Result<void> ViewportSession::recreate_swapchain()
{
    set_status_phase(ViewportPhase::Resizing, "Native viewport resizing");
    if (auto idle = presenter_->wait_idle(); !idle) {
        return std::unexpected(idle.error());
    }
    overlay_.reset_swapchain();
    if (auto recreated = presenter_->recreate_swapchain(); !recreated) {
        return std::unexpected(recreated.error());
    }
    update_swapchain_status();
    if (growth_ready_) {
        if (auto targets = overlay_.set_swapchain(presenter_->context(), presenter_->swapchain()); !targets) {
            return std::unexpected(targets.error());
        }
    }
    std::fprintf(stdout, "native viewport swapchain resized: %dx%d\n", info_.extent.width, info_.extent.height);
    std::fflush(stdout);
    needs_present_ = true;
    return {};
}

void ViewportSession::render_loop()
{
    auto report_failure = [this](std::string message) {
        set_status_error(message);
        std::fprintf(stderr, "native viewport failed: %s\n", message.c_str());
    };

    while (running_) {
        complete_pending_produce();
        if (growth_ready_ && slots_[present_slot_].timeline_value != 0 && !needs_present_.exchange(false)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(produce_pending_ ? 2 : 8));
            continue;
        }

        if (auto waited = presenter_->wait_for_frame_slot(); !waited) {
            report_failure(waited.error().message);
            break;
        }
        if (ensure_growth_renderer()) {
            (void)produce_growth_frame();
        }
        if (!growth_ready_ || slots_[present_slot_].timeline_value == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(produce_pending_ ? 2 : 8));
            continue;
        }

        auto acquired = presenter_->acquire_frame();
        if (!acquired) {
            report_failure(acquired.error().message);
            break;
        }
        if (!*acquired) {
            if (auto recreated = recreate_swapchain(); !recreated) {
                report_failure("native viewport resize failed: " + recreated.error().message);
                break;
            }
            continue;
        }

        const auto& frame = **acquired;
        record_growth_present(frame.command_buffer, frame.image_index);
        const GrowthSlot& presented_slot = slots_[present_slot_];
        auto presented = presenter_->submit_and_present(
            frame,
            {
                .semaphore = frames_ready_.vk_semaphore(),
                .value = presented_slot.timeline_value,
                .stages = VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            });
        if (!presented) {
            report_failure(presented.error().message);
            break;
        }
        if (*presented) {
            if (auto recreated = recreate_swapchain(); !recreated) {
                report_failure("native viewport resize failed: " + recreated.error().message);
                break;
            }
        }
        mark_frame_presented(presented_slot);

        if (produce_pending_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }

    (void)presenter_->wait_idle();
    if (produce_pending_ && copy_done_ != nullptr) {
        cudaEventSynchronize(copy_done_);
        produce_pending_ = false;
    }
    overlay_.reset();
    growth_ready_ = false;
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
}

bool ViewportSession::ensure_growth_renderer()
{
    if (growth_failed_) {
        return false;
    }
    if (growth_ready_) {
        return true;
    }

    render::GrowthPreviewStageProjection stage;
    {
        std::lock_guard lock(stage_mutex_);
        if (!pending_stage_) {
            return false;
        }
        stage = *pending_stage_;
    }
    set_status_phase(ViewportPhase::Warming, "Native RTX viewport warming");

    auto fail = [this](std::string message) {
        for (auto& slot : slots_) {
            slot.color.reset();
            slot.distance.reset();
        }
        overlay_.reset();
        frames_ready_.reset();
        if (copy_done_ != nullptr) {
            cudaEventDestroy(copy_done_);
            copy_done_ = nullptr;
        }
        renderer_.reset();
        growth_ready_ = false;
        growth_failed_ = true;
        set_status_error(std::move(message));
        return false;
    };

    const int cuda_device = presenter_->context_info().cuda_device;
    if (auto selected = select_cuda_device(cuda_device); !selected) {
        return fail("CUDA device selection failed: " + selected.error().message);
    }

    auto renderer = ovrtx::RendererSession::create({.asset_search_path = stage.usd_stage.asset_search_path});
    if (!renderer) {
        return fail("ovrtx renderer creation failed: " + renderer.error().message);
    }
    renderer_ = std::make_unique<ovrtx::RendererSession>(std::move(*renderer));

    for (auto& slot : slots_) {
        auto color = CudaInteropImage::create(presenter_->context(), stage.usd_stage.width, stage.usd_stage.height);
        if (!color) {
            return fail("Growth preview color image creation failed: " + color.error().message);
        }
        slot.color = std::move(*color);
    }

    auto frames_ready = CudaInteropTimelineSemaphore::create(presenter_->context());
    if (!frames_ready) {
        return fail("Growth preview timeline creation failed: " + frames_ready.error().message);
    }
    frames_ready_ = std::move(*frames_ready);

    if (const auto result = cudaEventCreateWithFlags(&copy_done_, cudaEventDisableTiming); result != cudaSuccess) {
        copy_done_ = nullptr;
        return fail(std::string("Growth preview CUDA event creation failed: ") + cudaGetErrorString(result));
    }

    for (auto& slot : slots_) {
        auto distance =
            CudaInteropFloatImage::create(presenter_->context(), stage.usd_stage.width, stage.usd_stage.height);
        if (!distance) {
            return fail("Growth preview depth image creation failed: " + distance.error().message);
        }
        slot.distance = std::move(*distance);
    }
    auto overlay = ViewportOverlay::create(presenter_->context(), presenter_->swapchain().format());
    if (!overlay) {
        return fail("Growth preview overlay creation failed: " + overlay.error().message);
    }
    overlay_ = std::move(*overlay);
    if (auto targets = overlay_.set_swapchain(presenter_->context(), presenter_->swapchain()); !targets) {
        return fail("Growth preview overlay target creation failed: " + targets.error().message);
    }
    for (std::uint32_t index = 0; index < ViewportOverlay::kDistanceSlotCount; ++index) {
        if (auto wired = overlay_.set_scene_distance(index, slots_[index].distance.view()); !wired) {
            return fail("Growth preview depth binding failed: " + wired.error().message);
        }
    }
    growth_ready_ = true;
    set_status_phase(ViewportPhase::Rendering, "Native RTX viewport rendering");
    return true;
}

Result<void> ViewportSession::resize_growth_slot(int slot_index, int width, int height)
{
    if (produce_pending_) {
        return std::unexpected(make_error("cannot resize a Growth preview slot while a frame copy is pending"));
    }

    auto color = CudaInteropImage::create(presenter_->context(), width, height);
    if (!color) {
        return std::unexpected(color.error());
    }
    auto distance = CudaInteropFloatImage::create(presenter_->context(), width, height);
    if (!distance) {
        return std::unexpected(distance.error());
    }

    if (auto idle = presenter_->wait_idle(); !idle) {
        return std::unexpected(idle.error());
    }
    if (auto wired = overlay_.set_scene_distance(static_cast<std::uint32_t>(slot_index), distance->view()); !wired) {
        return std::unexpected(wired.error());
    }

    GrowthSlot& slot = slots_[slot_index];
    slot.color = std::move(*color);
    slot.distance = std::move(*distance);
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
    auto fail = [this](std::string message) {
        set_status_error(message);
        std::fprintf(stderr, "native viewport render failed: %s\n", message.c_str());
        return false;
    };

    std::optional<render::GrowthPreviewStageProjection> stage_to_submit;
    ViewportCameraFraming stage_framing = ViewportCameraFraming::PreserveOrbit;
    {
        std::lock_guard lock(stage_mutex_);
        if (stage_dirty_ && pending_stage_) {
            stage_to_submit = *pending_stage_;
            stage_framing = pending_stage_framing_;
            stage_dirty_ = false;
        }
    }

    bool apply_camera = stage_to_submit.has_value();
    std::optional<render::GrowthPreviewCamera> oriented_camera;
    {
        std::lock_guard lock(camera_mutex_);
        if (stage_to_submit) {
            base_camera_ = stage_to_submit->camera;
            const auto framed_orbit = render::orbit_view_from_camera(*base_camera_);
            if (!orbit_ || stage_framing == ViewportCameraFraming::AutoFrame) {
                orbit_ = framed_orbit;
            } else {
                orbit_->target = framed_orbit.target;
            }
        }
        if (base_camera_ && orbit_) {
            if (orbit_dirty_) {
                orbit_dirty_ = false;
                apply_camera = true;
            }
            if (apply_camera) {
                oriented_camera = render::apply_orbit_view(*base_camera_, *orbit_);
            }
        }
    }

    GrowthSlot& slot = slots_[produce_slot_];
    const bool want_distance = guides_visible_ && world_origin_axes_visible_ && slot.distance.view() != VK_NULL_HANDLE;
    const bool rendered_once = slots_[present_slot_].timeline_value != 0 || slot.timeline_value != 0;
    if (!stage_to_submit && !apply_camera && rendered_once && want_distance == last_draw_guides_) {
        return false;
    }

    if (stage_to_submit) {
        set_status_phase(ViewportPhase::Rendering, "Native RTX viewport rendering");
        if (auto submitted = renderer_->submit_growth_preview(*stage_to_submit); !submitted) {
            return fail("ovrtx stage submit failed: " + submitted.error().message);
        }
    }
    if (oriented_camera) {
        if (auto set = renderer_->set_growth_preview_camera(*oriented_camera); !set) {
            return fail("ovrtx camera update failed: " + set.error().message);
        }
    }

    auto rendered = renderer_->render_cuda_frame(want_distance ? ovrtx::RenderFrameOutputs::ColorAndDistance
                                                               : ovrtx::RenderFrameOutputs::Color);
    if (!rendered) {
        return fail("ovrtx frame render failed: " + rendered.error().message);
    }
    if (rendered->width != slot.color.width() || rendered->height != slot.color.height()) {
        if (auto resized = resize_growth_slot(produce_slot_, rendered->width, rendered->height); !resized) {
            return fail("Growth preview render resize failed: " + resized.error().message);
        }
    }

    if (auto copied = slot.color.copy_from_cuda_array(rendered->color_cuda_array, rendered->width, rendered->height,
                                                      rendered->sync_stream);
        !copied) {
        return fail("Growth preview color copy failed: " + copied.error().message);
    }
    slot.has_distance = false;
    if (want_distance && rendered->scene_distance_cuda_array != nullptr) {
        if (auto copied = slot.distance.copy_from_cuda_array(rendered->scene_distance_cuda_array, rendered->width,
                                                             rendered->height, rendered->sync_stream);
            !copied) {
            return fail("Growth preview depth copy failed: " + copied.error().message);
        }
        slot.has_distance = true;
    }
    slot.camera = rendered->camera;

    if (const auto result = cudaEventRecord(copy_done_, reinterpret_cast<cudaStream_t>(rendered->sync_stream));
        result != cudaSuccess) {
        return fail(std::string("Growth preview CUDA event record failed: ") + cudaGetErrorString(result));
    }
    if (auto signaled = frames_ready_.signal_from_ovrtx_stream(timeline_value_ + 1, rendered->sync_stream);
        !signaled) {
        return fail("Growth preview timeline signal failed: " + signaled.error().message);
    }
    slot.timeline_value = ++timeline_value_;
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
        set_status_error(std::string("Growth preview CUDA copy failed: ") + cudaGetErrorString(query));
        return;
    }
    std::swap(present_slot_, produce_slot_);
    const GrowthSlot& present = slots_[present_slot_];
    const GrowthSlot& standby = slots_[produce_slot_];
    if (standby.color.width() != present.color.width() || standby.color.height() != present.color.height()) {
        if (auto resized = resize_growth_slot(produce_slot_, present.color.width(), present.color.height()); !resized) {
            set_status_error("Growth preview standby resize failed: " + resized.error().message);
            std::fprintf(stderr, "native viewport standby resize failed: %s\n", resized.error().message.c_str());
        } else {
            std::fprintf(stdout, "Growth preview render resized: %dx%d\n", present.color.width(),
                         present.color.height());
            std::fflush(stdout);
        }
    }
    needs_present_ = true;
}

void ViewportSession::record_growth_present(VkCommandBuffer command_buffer, std::uint32_t image_index)
{
    GrowthSlot& slot = slots_[present_slot_];
    VkImage swapchain_image = presenter_->swapchain().images()[image_index];

    transition(command_buffer, slot.color.image(), VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
               VK_ACCESS_MEMORY_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
               VK_PIPELINE_STAGE_TRANSFER_BIT);
    transition(command_buffer, swapchain_image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0,
               VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

    const VkClearColorValue black{{0.0F, 0.0F, 0.0F, 1.0F}};
    const VkImageSubresourceRange range = whole_color_image();
    vkCmdClearColorImage(command_buffer, swapchain_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &black, 1, &range);

    const VkRect2D content = contained_rect(slot.color.width(), slot.color.height(), presenter_->swapchain().extent());
    VkImageBlit blit{};
    blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    blit.srcOffsets[1] = {slot.color.width(), slot.color.height(), 1};
    blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    blit.dstOffsets[0] = {content.offset.x, content.offset.y, 0};
    blit.dstOffsets[1] = {content.offset.x + static_cast<std::int32_t>(content.extent.width),
                          content.offset.y + static_cast<std::int32_t>(content.extent.height), 1};
    vkCmdBlitImage(command_buffer, slot.color.image(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, swapchain_image,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);

    transition(command_buffer, slot.color.image(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,
               VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_MEMORY_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
               VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);

    if (guides_visible_ && world_origin_axes_visible_ && slot.has_distance) {
        const auto lines = build_overlay_lines(slot.camera);
        if (overlay_.record(command_buffer, image_index, presenter_->swapchain().extent(), content,
                            to_overlay_camera(slot.camera), lines, overlay_depth_bias(slot.camera),
                            static_cast<std::uint32_t>(present_slot_))) {
            return;
        }
    }
    transition(command_buffer, swapchain_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
               VK_ACCESS_TRANSFER_WRITE_BIT, 0, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
}

} // namespace toi::viewport
