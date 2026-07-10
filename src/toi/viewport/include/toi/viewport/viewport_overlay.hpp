#pragma once

#include "toi/viewport/error.hpp"
#include "toi/viewport/vulkan_context.hpp"
#include "toi/viewport/vulkan_swapchain.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <span>
#include <vector>

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

// Draws guide lines (e.g. world-origin axes) over the presented growth frame,
// using the ovrtx camera projection so they align with the rendered scene.
class ViewportOverlay {
public:
    ViewportOverlay() = default;
    ViewportOverlay(const ViewportOverlay&) = delete;
    ViewportOverlay& operator=(const ViewportOverlay&) = delete;
    ViewportOverlay(ViewportOverlay&& other) noexcept;
    ViewportOverlay& operator=(ViewportOverlay&& other) noexcept;
    ~ViewportOverlay();

    [[nodiscard]] static Result<ViewportOverlay> create(VulkanContext& context, VkFormat swapchain_format);

    // One depth-occlusion descriptor set per double-buffered frame slot.
    static constexpr std::uint32_t kDistanceSlotCount = 2;

    // Rebuild the per-image framebuffers for the current swapchain.
    [[nodiscard]] Result<void> set_swapchain(VulkanContext& context, const VulkanSwapchain& swapchain);
    void reset_swapchain();

    // Point a slot's depth-occlusion sampler at its scene-distance image.
    [[nodiscard]] Result<void> set_scene_distance(std::uint32_t distance_slot, VkImageView distance_view);

    // Records a render pass drawing the lines onto the swapchain image (which
    // must be in TRANSFER_DST_OPTIMAL); leaves it in PRESENT_SRC_KHR. Samples
    // the given slot's scene-distance image for occlusion.
    [[nodiscard]] Result<void> record(VkCommandBuffer command_buffer, std::uint32_t image_index, VkExtent2D extent,
                                      VkRect2D content_rect, const OverlayCamera& camera,
                                      std::span<const OverlayLine> lines, float depth_bias,
                                      std::uint32_t distance_slot);

    void reset();

private:
    void move_from(ViewportOverlay& other) noexcept;

    VkDevice device_ = VK_NULL_HANDLE;
    VkRenderPass render_pass_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptor_set_layout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool_ = VK_NULL_HANDLE;
    VkDescriptorSet descriptor_sets_[kDistanceSlotCount]{};
    VkSampler sampler_ = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkBuffer vertex_buffer_ = VK_NULL_HANDLE;
    VkDeviceMemory vertex_memory_ = VK_NULL_HANDLE;
    void* vertex_mapped_ = nullptr;
    std::uint32_t vertex_capacity_ = 0;
    std::vector<VkImageView> image_views_;
    std::vector<VkFramebuffer> framebuffers_;
};

} // namespace toi::viewport
