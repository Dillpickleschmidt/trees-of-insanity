#include "toi/viewport/preview_renderer.hpp"

#include "gpu_checks.hpp"
#include "toi/ovrtx/renderer_session.hpp"
#include "toi/render/viewport_guides.hpp"

#include <cuda_runtime_api.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <utility>
#include <vector>

namespace toi::viewport {
namespace {

constexpr int kSlotCount = 2;
constexpr int kDisplayTextureExtent = 4096;
constexpr float kTwoPi = 2.0F * std::numbers::pi_v<float>;
constexpr float kDragDollyLogRadiusPerPixel = 0.01F;
constexpr float kWheelDollyLogRadiusPerStep = 0.12F;

OverlayCamera to_overlay_camera(const render::GrowthPreviewCamera& camera)
{
    return {
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

std::vector<OverlayLine> build_overlay_lines(const render::GrowthPreviewCamera& camera,
                                             std::span<const render::DiagnosticOverlayLine> diagnostics,
                                             bool include_guides)
{
    std::vector<OverlayLine> lines;
    if (include_guides) {
        const auto guides = render::make_growth_preview_guides(camera);
        lines.reserve(guides.lines.size() + diagnostics.size());
        for (const auto& line : guides.lines) {
            lines.push_back({
                .start = {line.start.x, line.start.y, line.start.z},
                .end = {line.end.x, line.end.y, line.end.z},
                .color = {line.color.x, line.color.y, line.color.z},
                .alpha = line.alpha,
            });
        }
    } else {
        lines.reserve(diagnostics.size());
    }
    for (const auto& line : diagnostics) {
        lines.push_back({
            .start = {line.start.x, line.start.y, line.start.z},
            .end = {line.end.x, line.end.y, line.end.z},
            .color = {line.color.x, line.color.y, line.color.z},
            .alpha = line.alpha,
        });
    }
    return lines;
}

std::vector<OverlaySurfaceVertex> build_overlay_surface(
    std::span<const render::DiagnosticOverlaySurfaceVertex> diagnostics)
{
    std::vector<OverlaySurfaceVertex> vertices;
    vertices.reserve(diagnostics.size());
    for (const auto& vertex : diagnostics) {
        vertices.push_back({
            .position = {vertex.position.x, vertex.position.y, vertex.position.z},
            .color = {vertex.color.x, vertex.color.y, vertex.color.z},
            .alpha = vertex.alpha,
        });
    }
    return vertices;
}

std::vector<OverlaySphere> build_overlay_spheres(
    const render::GrowthPreviewCamera& camera,
    std::span<const render::DiagnosticOverlaySphere> diagnostics)
{
    std::vector<OverlaySphere> spheres;
    spheres.reserve(diagnostics.size());
    for (const auto& sphere : diagnostics) {
        spheres.push_back({
            .center = {sphere.center.x, sphere.center.y, sphere.center.z},
            .radius = sphere.radius,
            .color = {sphere.color.x, sphere.color.y, sphere.color.z},
            .alpha = sphere.alpha,
        });
    }
    std::stable_sort(spheres.begin(), spheres.end(), [&](const auto& left, const auto& right) {
        const auto distance_squared = [&](const auto& sphere) {
            const float x = sphere.center[0] - camera.eye.x;
            const float y = sphere.center[1] - camera.eye.y;
            const float z = sphere.center[2] - camera.eye.z;
            return x * x + y * y + z * z;
        };
        return distance_squared(left) > distance_squared(right);
    });
    return spheres;
}

std::vector<ProjectedPlantDiagnosticLabel> project_diagnostic_labels(
    std::span<const render::PlantDiagnosticLabel> labels, const render::GrowthPreviewCamera& camera)
{
    const auto dot = [](growth::Vec3 left, growth::Vec3 right) {
        return left.x * right.x + left.y * right.y + left.z * right.z;
    };
    const auto forward = growth::scale(camera.negative_forward, -1.0F);
    std::vector<ProjectedPlantDiagnosticLabel> projected;
    projected.reserve(labels.size());
    for (const auto& label : labels) {
        const auto relative = growth::subtract(label.world_position, camera.eye);
        const float depth = dot(relative, forward);
        const float normalized_x = depth > camera.near_clip
                                       ? 0.5F + static_cast<float>(camera.focal_length / camera.horizontal_aperture) *
                                                    dot(relative, camera.right) / depth
                                       : 0.0F;
        const float normalized_y = depth > camera.near_clip
                                       ? 0.5F - static_cast<float>(camera.focal_length / camera.vertical_aperture) *
                                                    dot(relative, camera.up) / depth
                                       : 0.0F;
        projected.push_back({
            .module_id = label.module_id,
            .x = normalized_x,
            .y = normalized_y,
            .visible = depth > camera.near_clip && normalized_x >= 0.0F && normalized_x <= 1.0F &&
                       normalized_y >= 0.0F && normalized_y <= 1.0F,
            .direct_light_exposure = label.direct_light_exposure,
            .accumulated_light = label.accumulated_light,
            .vigor = label.vigor,
        });
    }
    return projected;
}

float overlay_animation_time()
{
    static const auto epoch = std::chrono::steady_clock::now();
    const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - epoch).count();
    return static_cast<float>(std::fmod(elapsed, 512.0));
}

float overlay_depth_bias(const render::GrowthPreviewCamera& camera)
{
    const float dx = camera.eye.x - camera.target.x;
    const float dy = camera.eye.y - camera.target.y;
    const float dz = camera.eye.z - camera.target.z;
    return std::max(0.01F, std::sqrt(dx * dx + dy * dy + dz * dz) * 0.0005F);
}

void transition(VkCommandBuffer command_buffer, VkImage image, VkImageLayout old_layout,
                VkImageLayout new_layout, VkAccessFlags source_access, VkAccessFlags destination_access,
                VkPipelineStageFlags source_stage, VkPipelineStageFlags destination_stage)
{
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = old_layout;
    barrier.newLayout = new_layout;
    barrier.srcAccessMask = source_access;
    barrier.dstAccessMask = destination_access;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(command_buffer, source_stage, destination_stage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

} // namespace

struct PreviewRenderer::Slot {
    CudaInteropImage color;
    CudaInteropImage distance;
    render::GrowthPreviewCamera camera;
    cudaEvent_t copy_done = nullptr;
    VkCommandBuffer command_buffer = VK_NULL_HANDLE;
    VkFence precompose_done = VK_NULL_HANDLE;
    std::uint64_t timeline_value = 0;
    bool rendering = false;
    bool ready = false;
    bool has_distance = false;
    std::vector<OverlayLine> overlay_lines;
    std::vector<OverlaySurfaceVertex> overlay_surface_vertices;
    std::vector<OverlaySphere> overlay_spheres;
    std::vector<ProjectedPlantDiagnosticLabel> projected_labels;
};

Result<std::unique_ptr<PreviewRenderer>> PreviewRenderer::create(
    PreviewRendererDevice device, render::GrowthPreviewStageProjection initial_stage, render::OrbitView initial_orbit)
{
    auto renderer = std::unique_ptr<PreviewRenderer>(new PreviewRenderer);
    if (auto initialized =
            renderer->initialize(std::move(device), std::move(initial_stage), initial_orbit); !initialized) {
        return std::unexpected(initialized.error());
    }
    return renderer;
}

PreviewRenderer::~PreviewRenderer()
{
    running_ = false;
    condition_.notify_all();
    if (thread_.joinable()) {
        thread_.join();
    }
    if (context_.device() != VK_NULL_HANDLE) {
        vkQueueWaitIdle(context_.graphics_queue());
    }
    renderer_.reset();
    overlay_.reset();
    frames_ready_.reset();
    if (slots_) {
        for (int index = 0; index < kSlotCount; ++index) {
            Slot& slot = slots_[index];
            slot.color.reset();
            slot.distance.reset();
            if (slot.copy_done != nullptr) {
                cudaEventDestroy(slot.copy_done);
            }
            if (slot.precompose_done != VK_NULL_HANDLE) {
                vkDestroyFence(context_.device(), slot.precompose_done, nullptr);
            }
        }
    }
    if (display_image_ != VK_NULL_HANDLE) {
        vkDestroyImage(context_.device(), display_image_, nullptr);
    }
    if (display_memory_ != VK_NULL_HANDLE) {
        vkFreeMemory(context_.device(), display_memory_, nullptr);
    }
    if (command_pool_ != VK_NULL_HANDLE) {
        vkDestroyCommandPool(context_.device(), command_pool_, nullptr);
    }
}

void PreviewRenderer::set_stage(render::GrowthPreviewStageProjection stage, render::OrbitView orbit)
{
    {
        std::lock_guard lock(mutex_);
        base_camera_ = stage.camera;
        orbit_ = orbit;
        pending_stage_ = std::move(stage);
        stage_dirty_ = true;
        camera_dirty_ = true;
        dirty_ = true;
    }
    condition_.notify_all();
}

void PreviewRenderer::set_orbit(render::OrbitView orbit)
{
    {
        std::lock_guard lock(mutex_);
        orbit_ = orbit;
        camera_dirty_ = true;
        dirty_ = true;
    }
    condition_.notify_all();
}

std::optional<render::OrbitView> PreviewRenderer::apply_camera_input(std::string_view kind, float dx, float dy,
                                                                     int viewport_height)
{
    std::optional<render::OrbitView> result;
    {
        std::lock_guard lock(mutex_);
        if (!base_camera_ || !orbit_) {
            return std::nullopt;
        }
        if (kind == "orbit") {
            const float radians_per_pixel = kTwoPi / static_cast<float>(std::max(1, viewport_height));
            orbit_ = render::rotate_orbit_view(*orbit_, -dx * radians_per_pixel, dy * radians_per_pixel);
        } else if (kind == "pan") {
            const auto camera = render::apply_orbit_view(*base_camera_, *orbit_);
            const float world_per_pixel = orbit_->radius *
                static_cast<float>(camera.vertical_aperture / camera.focal_length) /
                static_cast<float>(std::max(1, viewport_height));
            const auto horizontal = growth::scale(camera.right, -dx * world_per_pixel);
            const auto vertical = growth::scale(camera.up, dy * world_per_pixel);
            orbit_->target = growth::add(orbit_->target, growth::add(horizontal, vertical));
        } else if (kind == "dolly") {
            orbit_ = render::dolly_orbit_view(*orbit_, std::exp(dy * kDragDollyLogRadiusPerPixel));
        } else if (kind == "wheel") {
            orbit_ = render::dolly_orbit_view(*orbit_, std::exp(-dy * kWheelDollyLogRadiusPerStep));
        } else {
            return std::nullopt;
        }
        camera_dirty_ = true;
        dirty_ = true;
        result = orbit_;
    }
    condition_.notify_all();
    return result;
}

void PreviewRenderer::set_guide_options(bool guides_visible, bool world_origin_axes_visible)
{
    {
        std::lock_guard lock(mutex_);
        guides_visible_ = guides_visible;
        world_origin_axes_visible_ = world_origin_axes_visible;
        dirty_ = true;
    }
    condition_.notify_all();
}

void PreviewRenderer::set_max_frames_per_second(float frames_per_second)
{
    if (!std::isfinite(frames_per_second)) {
        return;
    }
    frames_per_second = std::clamp(frames_per_second, 1.0F, 240.0F);
    {
        std::lock_guard lock(mutex_);
        minimum_scene_frame_interval_ =
            std::chrono::milliseconds(std::max(1, static_cast<int>(std::ceil(1000.0F / frames_per_second))));
        next_scene_frame_time_ = {};
        next_overlay_frame_time_ = {};
    }
    condition_.notify_all();
}

void PreviewRenderer::set_frame_ready_callback(std::function<void()> callback)
{
    std::lock_guard lock(mutex_);
    frame_ready_callback_ = std::move(callback);
}

void PreviewRenderer::set_diagnostic_labels_callback(
    std::function<void(std::vector<ProjectedPlantDiagnosticLabel>)> callback)
{
    std::lock_guard lock(mutex_);
    diagnostic_labels_callback_ = std::move(callback);
}

bool PreviewRenderer::prepare_frame_on_render_thread()
{
    int resize_width = 0;
    int resize_height = 0;
    {
        std::lock_guard lock(mutex_);
        if (resize_waiting_) {
            resize_width = requested_width_;
            resize_height = requested_height_;
        }
    }
    if (resize_width > 0 && resize_height > 0) {
        auto resized = resize_frame_resources_on_render_thread(resize_width, resize_height);
        {
            std::lock_guard lock(mutex_);
            resize_waiting_ = false;
            if (resized) {
                width_ = resize_width;
                height_ = resize_height;
                displayed_slot_ = -1;
                ready_slot_ = -1;
                dirty_ = true;
                status_.width = width_;
                status_.height = height_;
                status_.phase = "rendering";
                status_.message = "Qt RTX viewport resized";
            } else {
                dirty_ = false;
                stage_dirty_ = false;
                status_.phase = "error";
                status_.message = resized.error().message;
            }
        }
        condition_.notify_all();
        return false;
    }

    int slot_index = -1;
    int old_displayed_slot = -1;
    bool overlay_only = false;
    {
        std::lock_guard lock(mutex_);
        const auto now = std::chrono::steady_clock::now();
        if (ready_slot_ >= 0) {
            slot_index = ready_slot_;
            ready_slot_ = -1;
            old_displayed_slot = displayed_slot_;
            displayed_slot_ = slot_index;
            slots_[slot_index].ready = false;
            next_overlay_frame_time_ = now + minimum_scene_frame_interval_;
        } else if (displayed_slot_ >= 0 &&
                   !slots_[displayed_slot_].overlay_surface_vertices.empty()) {
            if (next_overlay_frame_time_ > now) {
                return false;
            }
            slot_index = displayed_slot_;
            overlay_only = true;
            next_overlay_frame_time_ = now + minimum_scene_frame_interval_;
        } else {
            return false;
        }
    }

    Slot& slot = slots_[slot_index];
    vkWaitForFences(context_.device(), 1, &slot.precompose_done, VK_TRUE, UINT64_MAX);
    vkResetFences(context_.device(), 1, &slot.precompose_done);
    vkResetCommandBuffer(slot.command_buffer, 0);
    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(slot.command_buffer, &begin);

    transition(slot.command_buffer, slot.color.image(), VK_IMAGE_LAYOUT_GENERAL,
               VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_MEMORY_WRITE_BIT,
               VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
               VK_PIPELINE_STAGE_TRANSFER_BIT);
    transition(slot.command_buffer, display_image_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_SHADER_READ_BIT,
               VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
               VK_PIPELINE_STAGE_TRANSFER_BIT);

    VkImageBlit blit{};
    blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    blit.srcOffsets[1] = {slot.color.width(), slot.color.height(), 1};
    blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    blit.dstOffsets[1] = {width_, height_, 1};
    vkCmdBlitImage(slot.command_buffer, slot.color.image(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   display_image_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);

    transition(slot.command_buffer, slot.color.image(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
               VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_MEMORY_WRITE_BIT,
               VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
    if (slot.has_distance) {
        if (auto recorded = overlay_.record(
                slot.command_buffer,
                {static_cast<std::uint32_t>(width_), static_cast<std::uint32_t>(height_)},
                {{0, 0}, {static_cast<std::uint32_t>(width_), static_cast<std::uint32_t>(height_)}},
                to_overlay_camera(slot.camera), slot.overlay_lines, slot.overlay_surface_vertices,
                slot.overlay_spheres,
                overlay_depth_bias(slot.camera), overlay_animation_time(),
                static_cast<std::uint32_t>(slot_index), !overlay_only); !recorded) {
            vkEndCommandBuffer(slot.command_buffer);
            set_error("Vulkan guide overlay failed: " + recorded.error().message);
            return false;
        }
    } else {
        transition(slot.command_buffer, display_image_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
                   VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    }
    vkEndCommandBuffer(slot.command_buffer);

    const VkSemaphore semaphore = frames_ready_.vk_semaphore();
    const VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    VkTimelineSemaphoreSubmitInfo timeline{};
    timeline.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
    timeline.waitSemaphoreValueCount = 1;
    timeline.pWaitSemaphoreValues = &slot.timeline_value;
    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.pNext = &timeline;
    submit.waitSemaphoreCount = 1;
    submit.pWaitSemaphores = &semaphore;
    submit.pWaitDstStageMask = &wait_stage;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &slot.command_buffer;
    if (vkQueueSubmit(context_.graphics_queue(), 1, &submit, slot.precompose_done) != VK_SUCCESS) {
        set_error("Vulkan preview precomposition submit failed");
        return false;
    }

    std::function<void(std::vector<ProjectedPlantDiagnosticLabel>)> labels_callback;
    std::function<void()> animation_callback;
    std::vector<ProjectedPlantDiagnosticLabel> projected_labels;
    {
        std::lock_guard lock(mutex_);
        presented_width_ = width_;
        presented_height_ = height_;
        status_.phase = "ready";
        status_.message = "Qt RTX viewport ready";
        ++status_.precomposition_count;
        status_.frame_generation = status_.precomposition_count;
        if (!overlay_only) {
            labels_callback = diagnostic_labels_callback_;
            projected_labels = slot.projected_labels;
        }
        if (!slot.overlay_surface_vertices.empty()) {
            animation_callback = frame_ready_callback_;
        }
        if (old_displayed_slot >= 0) condition_.notify_all();
    }
    if (labels_callback) labels_callback(std::move(projected_labels));
    if (animation_callback) animation_callback();
    return true;
}

VkImage PreviewRenderer::display_image() const { return display_image_; }
VkImageLayout PreviewRenderer::display_layout() const { return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; }
int PreviewRenderer::width() const { return presented_width_; }
int PreviewRenderer::height() const { return presented_height_; }
int PreviewRenderer::texture_width() const { return kDisplayTextureExtent; }
int PreviewRenderer::texture_height() const { return kDisplayTextureExtent; }

PreviewRendererStatus PreviewRenderer::status() const
{
    std::lock_guard lock(mutex_);
    return status_;
}

Result<void> PreviewRenderer::initialize(PreviewRendererDevice device,
                                         render::GrowthPreviewStageProjection initial_stage,
                                         render::OrbitView initial_orbit)
{
    width_ = std::clamp(initial_stage.usd_stage.width, 1, kDisplayTextureExtent);
    height_ = std::clamp(initial_stage.usd_stage.height, 1, kDisplayTextureExtent);
    presented_width_ = width_;
    presented_height_ = height_;

    VkCommandPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool_info.queueFamilyIndex = device.queue_family;
    if (vkCreateCommandPool(device.device, &pool_info, nullptr, &command_pool_) != VK_SUCCESS) {
        return std::unexpected(make_error("Vulkan preview command pool creation failed"));
    }
    context_ = VulkanContext::borrow(device.physical_device, device.device, device.queue,
                                    device.queue_family, command_pool_, device.cuda_device,
                                    std::move(device.device_name));
    if (auto selected = select_cuda_device(context_.info().cuda_device); !selected) {
        return std::unexpected(selected.error());
    }

    slots_ = std::make_unique<Slot[]>(kSlotCount);
    VkCommandBufferAllocateInfo command_info{};
    command_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    command_info.commandPool = command_pool_;
    command_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    command_info.commandBufferCount = kSlotCount;
    std::array<VkCommandBuffer, kSlotCount> command_buffers{};
    if (vkAllocateCommandBuffers(context_.device(), &command_info, command_buffers.data()) != VK_SUCCESS) {
        return std::unexpected(make_error("Vulkan preview command buffer allocation failed"));
    }
    for (int index = 0; index < kSlotCount; ++index) {
        auto color = CudaInteropImage::create_color(context_, width_, height_);
        if (!color) {
            return std::unexpected(color.error());
        }
        slots_[index].color = std::move(*color);
        auto distance = CudaInteropImage::create_distance(context_, width_, height_);
        if (!distance) {
            return std::unexpected(distance.error());
        }
        slots_[index].distance = std::move(*distance);
        slots_[index].command_buffer = command_buffers[index];
        if (cudaEventCreateWithFlags(&slots_[index].copy_done, cudaEventDisableTiming) != cudaSuccess) {
            return std::unexpected(make_error("CUDA preview event creation failed"));
        }
        VkFenceCreateInfo fence_info{};
        fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        if (vkCreateFence(context_.device(), &fence_info, nullptr, &slots_[index].precompose_done) != VK_SUCCESS) {
            return std::unexpected(make_error("Vulkan preview fence creation failed"));
        }
    }
    auto timeline = CudaInteropTimelineSemaphore::create(context_);
    if (!timeline) {
        return std::unexpected(timeline.error());
    }
    frames_ready_ = std::move(*timeline);

    VkImageCreateInfo image_info{};
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.format = VK_FORMAT_R8G8B8A8_UNORM;
    image_info.extent = {kDisplayTextureExtent, kDisplayTextureExtent, 1};
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(context_.device(), &image_info, nullptr, &display_image_) != VK_SUCCESS) {
        return std::unexpected(make_error("Vulkan display image creation failed"));
    }
    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(context_.device(), display_image_, &requirements);
    const auto memory_type = find_memory_type(context_.physical_device(), requirements.memoryTypeBits,
                                              VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (!memory_type) {
        return std::unexpected(memory_type.error());
    }
    VkMemoryAllocateInfo allocate{};
    allocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocate.allocationSize = requirements.size;
    allocate.memoryTypeIndex = *memory_type;
    if (vkAllocateMemory(context_.device(), &allocate, nullptr, &display_memory_) != VK_SUCCESS ||
        vkBindImageMemory(context_.device(), display_image_, display_memory_, 0) != VK_SUCCESS) {
        return std::unexpected(make_error("Vulkan display image allocation failed"));
    }

    auto overlay = ViewportOverlay::create(context_, VK_FORMAT_R8G8B8A8_UNORM);
    if (!overlay) {
        return std::unexpected(overlay.error());
    }
    overlay_ = std::move(*overlay);
    if (auto target = overlay_.set_target(display_image_, VK_FORMAT_R8G8B8A8_UNORM,
                                          {kDisplayTextureExtent, kDisplayTextureExtent});
        !target) {
        return std::unexpected(target.error());
    }
    for (std::uint32_t index = 0; index < kSlotCount; ++index) {
        if (auto wired = overlay_.set_scene_distance(index, slots_[index].distance.view()); !wired) {
            return std::unexpected(wired.error());
        }
    }

    // Establish the persistent layout expected by Qt before the image is imported.
    VkCommandBuffer command_buffer = slots_[0].command_buffer;
    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(command_buffer, &begin);
    transition(command_buffer, display_image_, VK_IMAGE_LAYOUT_UNDEFINED,
               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT,
               VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
    const VkClearColorValue clear_color{{0.02F, 0.02F, 0.02F, 1.0F}};
    const VkImageSubresourceRange color_range{
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel = 0,
        .levelCount = 1,
        .baseArrayLayer = 0,
        .layerCount = 1,
    };
    vkCmdClearColorImage(command_buffer, display_image_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                         &clear_color, 1, &color_range);
    transition(command_buffer, display_image_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
               VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
               VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    vkEndCommandBuffer(command_buffer);
    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &command_buffer;
    vkQueueSubmit(context_.graphics_queue(), 1, &submit, VK_NULL_HANDLE);
    vkQueueWaitIdle(context_.graphics_queue());
    vkResetCommandBuffer(command_buffer, 0);

    status_ = {
        .phase = "warming",
        .message = "Qt RTX viewport warming",
        .width = width_,
        .height = height_,
    };
    base_camera_ = initial_stage.camera;
    orbit_ = initial_orbit;
    pending_stage_ = std::move(initial_stage);
    stage_dirty_ = true;
    camera_dirty_ = true;
    dirty_ = true;
    running_ = true;
    thread_ = std::thread(&PreviewRenderer::render_loop, this);
    return {};
}

Result<void> PreviewRenderer::resize_frame_resources_on_render_thread(int width, int height)
{
    for (int index = 0; index < kSlotCount; ++index) {
        Slot& slot = slots_[index];
        vkWaitForFences(context_.device(), 1, &slot.precompose_done, VK_TRUE, UINT64_MAX);
        slot.color.reset();
        slot.distance.reset();

        auto color = CudaInteropImage::create_color(context_, width, height);
        if (!color) {
            return std::unexpected(color.error());
        }
        auto distance = CudaInteropImage::create_distance(context_, width, height);
        if (!distance) {
            return std::unexpected(distance.error());
        }
        slot.color = std::move(*color);
        slot.distance = std::move(*distance);
        slot.ready = false;
        slot.rendering = false;
        slot.has_distance = false;
        if (auto wired = overlay_.set_scene_distance(static_cast<std::uint32_t>(index), slot.distance.view()); !wired) {
            return std::unexpected(wired.error());
        }
    }
    return {};
}

void PreviewRenderer::render_loop()
{
    std::filesystem::path asset_search_path;
    {
        std::lock_guard lock(mutex_);
        if (!pending_stage_) {
            status_.phase = "error";
            status_.message = "Initial preview stage is missing";
            return;
        }
        asset_search_path = pending_stage_->usd_stage.asset_search_path;
    }
    auto renderer = ovrtx::RendererSession::create({.asset_search_path = std::move(asset_search_path)});
    if (!renderer) {
        set_error("ovrtx renderer creation failed: " + renderer.error().message);
        return;
    }
    renderer_ = std::make_unique<ovrtx::RendererSession>(std::move(*renderer));

    while (running_) {
        render::GrowthPreviewStageProjection stage;
        std::optional<render::GrowthPreviewCamera> camera;
        bool submit_stage = false;
        bool update_camera = false;
        bool draw_guides = false;
        bool draw_overlay = false;
        int slot_index = -1;
        {
            std::unique_lock lock(mutex_);
            condition_.wait(lock, [this] {
                if (!running_) return true;
                if (!dirty_ || ready_slot_ >= 0) return false;
                for (int index = 0; index < kSlotCount; ++index) {
                    if (index != displayed_slot_ && !slots_[index].rendering && !slots_[index].ready) return true;
                }
                return false;
            });
            if (!running_) break;
            if (pending_stage_ &&
                (pending_stage_->usd_stage.width != width_ || pending_stage_->usd_stage.height != height_)) {
                requested_width_ = std::clamp(pending_stage_->usd_stage.width, 1, kDisplayTextureExtent);
                requested_height_ = std::clamp(pending_stage_->usd_stage.height, 1, kDisplayTextureExtent);
                resize_waiting_ = true;
                status_.phase = "resizing";
                status_.message = "Qt RTX viewport resizing";
                auto callback = frame_ready_callback_;
                lock.unlock();
                if (callback) callback();
                lock.lock();
                condition_.wait(lock, [this] { return !running_ || !resize_waiting_; });
                continue;
            }
            const auto now = std::chrono::steady_clock::now();
            if (next_scene_frame_time_ > now) {
                condition_.wait_until(lock, next_scene_frame_time_);
                continue;
            }
            next_scene_frame_time_ = now + minimum_scene_frame_interval_;
            for (int index = 0; index < kSlotCount; ++index) {
                if (index != displayed_slot_ && !slots_[index].rendering && !slots_[index].ready) {
                    slot_index = index;
                    break;
                }
            }
            if (slot_index < 0 || !pending_stage_) continue;
            stage = *pending_stage_;
            submit_stage = stage_dirty_;
            stage_dirty_ = false;
            update_camera = camera_dirty_;
            if (base_camera_ && orbit_) {
                camera = render::apply_orbit_view(*base_camera_, *orbit_);
            }
            camera_dirty_ = false;
            draw_guides = guides_visible_ && world_origin_axes_visible_;
            draw_overlay = draw_guides || !stage.diagnostic_lines.empty() ||
                !stage.diagnostic_surface_vertices.empty() || !stage.diagnostic_spheres.empty();
            dirty_ = false;
            slots_[slot_index].rendering = true;
            status_.phase = "rendering";
            status_.message = "Qt RTX viewport rendering";
        }

        Slot& slot = slots_[slot_index];
        vkWaitForFences(context_.device(), 1, &slot.precompose_done, VK_TRUE, UINT64_MAX);
        auto fail = [this, slot_index](std::string message) {
            {
                std::lock_guard lock(mutex_);
                slots_[slot_index].rendering = false;
            }
            set_error(std::move(message));
        };
        if (submit_stage) {
            if (auto submitted = renderer_->submit_growth_preview(stage); !submitted) {
                fail("ovrtx stage submit failed: " + submitted.error().message);
                continue;
            }
        }
        if (update_camera && camera) {
            if (auto updated = renderer_->set_growth_preview_camera(*camera); !updated) {
                fail("ovrtx camera update failed: " + updated.error().message);
                continue;
            }
        }
        auto rendered = renderer_->render_cuda_frame(
            draw_overlay ? ovrtx::RenderFrameOutputs::ColorAndDistance : ovrtx::RenderFrameOutputs::Color);
        if (!rendered) {
            fail("ovrtx frame render failed: " + rendered.error().message);
            continue;
        }
        if (rendered->width != width_ || rendered->height != height_) {
            fail("ovrtx frame extent changed unexpectedly");
            continue;
        }
        if (auto copied = slot.color.copy_from_cuda_array(rendered->color_cuda_array, rendered->width,
                                                          rendered->height, rendered->sync_stream); !copied) {
            fail("CUDA preview copy failed: " + copied.error().message);
            continue;
        }
        slot.has_distance = false;
        if (draw_overlay && rendered->scene_distance_cuda_array != nullptr) {
            if (auto copied = slot.distance.copy_from_cuda_array(rendered->scene_distance_cuda_array,
                                                                 rendered->width, rendered->height,
                                                                 rendered->sync_stream); !copied) {
                fail("CUDA preview distance copy failed: " + copied.error().message);
                continue;
            }
            slot.has_distance = true;
        }
        slot.camera = camera.value_or(stage.camera);
        slot.overlay_lines = build_overlay_lines(slot.camera, stage.diagnostic_lines, draw_guides);
        slot.overlay_surface_vertices = build_overlay_surface(stage.diagnostic_surface_vertices);
        slot.overlay_spheres = build_overlay_spheres(slot.camera, stage.diagnostic_spheres);
        slot.projected_labels = project_diagnostic_labels(stage.diagnostic_labels, slot.camera);
        const std::uint64_t timeline_value = ++next_timeline_value_;
        if (auto signaled = frames_ready_.signal_from_ovrtx_stream(timeline_value, rendered->sync_stream); !signaled) {
            fail("CUDA preview timeline signal failed: " + signaled.error().message);
            continue;
        }
        if (cudaEventRecord(slot.copy_done, reinterpret_cast<cudaStream_t>(rendered->sync_stream)) != cudaSuccess ||
            cudaEventSynchronize(slot.copy_done) != cudaSuccess) {
            fail("CUDA preview completion failed");
            continue;
        }

        std::function<void()> callback;
        {
            std::lock_guard lock(mutex_);
            slot.timeline_value = timeline_value;
            slot.rendering = false;
            slot.ready = true;
            ready_slot_ = slot_index;
            ++status_.scene_frame_count;
            callback = frame_ready_callback_;
        }
        if (callback) callback();
    }
}

void PreviewRenderer::set_error(std::string message)
{
    std::lock_guard lock(mutex_);
    status_.phase = "error";
    status_.message = std::move(message);
}

} // namespace toi::viewport
