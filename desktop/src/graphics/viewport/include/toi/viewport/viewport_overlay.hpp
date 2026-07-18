#pragma once

#include "toi/viewport/error.hpp"
#include "toi/viewport/vulkan_context.hpp"

#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>
#include <span>

namespace toi::viewport {

struct OverlayCamera {
    float eye[3]{};
    float right[3]{};
    float up[3]{};
    float negative_forward[3]{};
    float focal_length = 0.0F;
    float horizontal_aperture = 0.0F;
    float vertical_aperture = 0.0F;
    float near_clip = 0.1F;
    float far_clip = 100000.0F;
};

struct OverlayLine {
    float start[3]{};
    float end[3]{};
    float color[3]{};
    float alpha = 1.0F;
};

struct OverlaySurfaceVertex {
    float position[3]{};
    float color[3]{};
    float alpha = 1.0F;
};

struct OverlaySphere {
    float center[3]{};
    float radius = 0.0F;
    float color[3]{};
    float alpha = 1.0F;
};

class ViewportOverlay {
public:
    static constexpr std::uint32_t kDistanceSlotCount = 2;

    ViewportOverlay() = default;
    ViewportOverlay(const ViewportOverlay&) = delete;
    ViewportOverlay& operator=(const ViewportOverlay&) = delete;
    ViewportOverlay(ViewportOverlay&& other) noexcept;
    ViewportOverlay& operator=(ViewportOverlay&& other) noexcept;
    ~ViewportOverlay();

    [[nodiscard]] static Result<ViewportOverlay> create(VulkanContext& context, VkFormat target_format);
    [[nodiscard]] Result<void> set_target(VkImage target, VkFormat format, VkExtent2D extent);
    [[nodiscard]] Result<void> set_scene_distance(std::uint32_t slot, VkImageView distance_view);
    [[nodiscard]] Result<void> record(VkCommandBuffer command_buffer, VkExtent2D extent, VkRect2D content_rect,
                                      const OverlayCamera& camera, std::span<const OverlayLine> lines,
                                      std::span<const OverlaySurfaceVertex> surface_vertices,
                                      std::span<const OverlaySphere> spheres, float depth_bias,
                                      float animation_time, std::uint32_t distance_slot,
                                      bool upload_geometry = true);
    void reset();

private:
    [[nodiscard]] Result<void> ensure_surface_capacity(std::uint32_t slot, std::size_t vertex_count);
    void reset_target();
    void move_from(ViewportOverlay& other) noexcept;

    VkPhysicalDevice physical_device_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkRenderPass render_pass_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptor_set_layout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool_ = VK_NULL_HANDLE;
    VkDescriptorSet descriptor_sets_[kDistanceSlotCount]{};
    VkSampler sampler_ = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;
    VkPipeline line_pipeline_ = VK_NULL_HANDLE;
    VkPipeline surface_pipeline_ = VK_NULL_HANDLE;
    VkPipeline sphere_pipeline_ = VK_NULL_HANDLE;
    VkBuffer vertex_buffer_ = VK_NULL_HANDLE;
    VkDeviceMemory vertex_memory_ = VK_NULL_HANDLE;
    void* vertex_mapped_ = nullptr;
    VkBuffer surface_buffers_[kDistanceSlotCount]{};
    VkDeviceMemory surface_memories_[kDistanceSlotCount]{};
    void* surface_mapped_[kDistanceSlotCount]{};
    std::size_t surface_capacities_[kDistanceSlotCount]{};
    std::uint32_t surface_vertex_counts_[kDistanceSlotCount]{};
    VkImageView target_view_ = VK_NULL_HANDLE;
    VkFramebuffer framebuffer_ = VK_NULL_HANDLE;
};

} // namespace toi::viewport
