#include "toi/viewport/viewport_overlay.hpp"

#include <array>
#include <cstddef>
#include <cstring>
#include <cmath>
#include <sstream>
#include <string_view>
#include <utility>

// SPIR-V compiled and embedded as byte arrays by the build.
extern unsigned char overlay_lines_vert_spv[];
extern unsigned int overlay_lines_vert_spv_len;
extern unsigned char overlay_lines_frag_spv[];
extern unsigned int overlay_lines_frag_spv_len;

namespace toi::viewport {
namespace {

constexpr std::uint32_t kMaxLines = 4096;

struct OverlayVertex {
    float position[3];
    float color[3];
    float alpha;
    float path_distance;
    float dash_direction;
    float surface_tangent[3];
    float surface_radius;
    float screen_offset_pixels;
};

struct OverlayPushConstants {
    float eye[4];
    float right[4];
    float up[4];
    float negative_forward[4];
    float projection[4]; // focal_length, horizontal_aperture, vertical_aperture, near_clip
    float depth[4];      // far_clip, depth_bias, unused, unused
    float viewport[4];   // x, y, width, height of the contained rendered image
};

[[nodiscard]] Result<void> require_vk(VkResult result, std::string_view context)
{
    if (result != VK_SUCCESS) {
        std::ostringstream out;
        out << context << " failed: VkResult " << static_cast<int>(result);
        return std::unexpected(make_error(out.str()));
    }
    return {};
}

[[nodiscard]] Result<VkShaderModule> create_shader(VkDevice device, const unsigned char* code, std::uint32_t size)
{
    VkShaderModuleCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info.codeSize = size;
    info.pCode = reinterpret_cast<const std::uint32_t*>(code);
    VkShaderModule module = VK_NULL_HANDLE;
    if (auto result = require_vk(vkCreateShaderModule(device, &info, nullptr, &module), "vkCreateShaderModule");
        !result) {
        return std::unexpected(result.error());
    }
    return module;
}

[[nodiscard]] Result<std::uint32_t> find_memory_type(VkPhysicalDevice physical_device, std::uint32_t type_filter,
                                                     VkMemoryPropertyFlags properties)
{
    VkPhysicalDeviceMemoryProperties memory_properties{};
    vkGetPhysicalDeviceMemoryProperties(physical_device, &memory_properties);
    for (std::uint32_t index = 0; index < memory_properties.memoryTypeCount; ++index) {
        if ((type_filter & (1U << index)) != 0 &&
            (memory_properties.memoryTypes[index].propertyFlags & properties) == properties) {
            return index;
        }
    }
    return std::unexpected(make_error("no host-visible memory type for the overlay vertex buffer"));
}

} // namespace

Result<ViewportOverlay> ViewportOverlay::create(VulkanContext& context, VkFormat viewport_format)
{
    ViewportOverlay overlay;
    overlay.device_ = context.device();

    // Render pass: load the blitted frame, draw lines, present.
    VkAttachmentDescription color{};
    color.format = viewport_format;
    color.samples = VK_SAMPLE_COUNT_1_BIT;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color.initialLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    color.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentReference color_ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color_ref;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    // The LOAD op reads the blitted frame, so the load must wait for the blit
    // (READ), not just the draw (WRITE) — otherwise it loads undefined (black).
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo render_pass_info{};
    render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    render_pass_info.attachmentCount = 1;
    render_pass_info.pAttachments = &color;
    render_pass_info.subpassCount = 1;
    render_pass_info.pSubpasses = &subpass;
    render_pass_info.dependencyCount = 1;
    render_pass_info.pDependencies = &dependency;
    if (auto result = require_vk(vkCreateRenderPass(overlay.device_, &render_pass_info, nullptr, &overlay.render_pass_),
                                 "vkCreateRenderPass");
        !result) {
        return std::unexpected(result.error());
    }

    // Descriptor set for the scene-distance sampler (depth-aware occlusion).
    VkDescriptorSetLayoutBinding sampler_binding{};
    sampler_binding.binding = 0;
    sampler_binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    sampler_binding.descriptorCount = 1;
    sampler_binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo set_layout_info{};
    set_layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    set_layout_info.bindingCount = 1;
    set_layout_info.pBindings = &sampler_binding;
    if (auto result = require_vk(vkCreateDescriptorSetLayout(overlay.device_, &set_layout_info, nullptr,
                                                            &overlay.descriptor_set_layout_),
                                 "vkCreateDescriptorSetLayout");
        !result) {
        overlay.reset();
        return std::unexpected(result.error());
    }

    VkDescriptorPoolSize pool_size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kDistanceSlotCount};
    VkDescriptorPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.maxSets = kDistanceSlotCount;
    pool_info.poolSizeCount = 1;
    pool_info.pPoolSizes = &pool_size;
    if (auto result = require_vk(vkCreateDescriptorPool(overlay.device_, &pool_info, nullptr, &overlay.descriptor_pool_),
                                 "vkCreateDescriptorPool");
        !result) {
        overlay.reset();
        return std::unexpected(result.error());
    }

    std::array<VkDescriptorSetLayout, kDistanceSlotCount> set_layouts{};
    set_layouts.fill(overlay.descriptor_set_layout_);
    VkDescriptorSetAllocateInfo set_alloc{};
    set_alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    set_alloc.descriptorPool = overlay.descriptor_pool_;
    set_alloc.descriptorSetCount = kDistanceSlotCount;
    set_alloc.pSetLayouts = set_layouts.data();
    if (auto result = require_vk(vkAllocateDescriptorSets(overlay.device_, &set_alloc, overlay.descriptor_sets_),
                                 "vkAllocateDescriptorSets");
        !result) {
        overlay.reset();
        return std::unexpected(result.error());
    }

    VkSamplerCreateInfo sampler_info{};
    sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler_info.magFilter = VK_FILTER_NEAREST;
    sampler_info.minFilter = VK_FILTER_NEAREST;
    sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (auto result = require_vk(vkCreateSampler(overlay.device_, &sampler_info, nullptr, &overlay.sampler_),
                                 "vkCreateSampler");
        !result) {
        overlay.reset();
        return std::unexpected(result.error());
    }

    auto vert = create_shader(overlay.device_, overlay_lines_vert_spv, overlay_lines_vert_spv_len);
    if (!vert) {
        overlay.reset();
        return std::unexpected(vert.error());
    }
    auto frag = create_shader(overlay.device_, overlay_lines_frag_spv, overlay_lines_frag_spv_len);
    if (!frag) {
        vkDestroyShaderModule(overlay.device_, *vert, nullptr);
        overlay.reset();
        return std::unexpected(frag.error());
    }

    VkPushConstantRange push_range{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                                   sizeof(OverlayPushConstants)};
    VkPipelineLayoutCreateInfo layout_info{};
    layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout_info.setLayoutCount = 1;
    layout_info.pSetLayouts = &overlay.descriptor_set_layout_;
    layout_info.pushConstantRangeCount = 1;
    layout_info.pPushConstantRanges = &push_range;
    auto layout_result =
        require_vk(vkCreatePipelineLayout(overlay.device_, &layout_info, nullptr, &overlay.pipeline_layout_),
                   "vkCreatePipelineLayout");
    if (!layout_result) {
        vkDestroyShaderModule(overlay.device_, *vert, nullptr);
        vkDestroyShaderModule(overlay.device_, *frag, nullptr);
        overlay.reset();
        return std::unexpected(layout_result.error());
    }

    std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = *vert;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = *frag;
    stages[1].pName = "main";

    VkVertexInputBindingDescription binding{0, sizeof(OverlayVertex), VK_VERTEX_INPUT_RATE_VERTEX};
    std::array<VkVertexInputAttributeDescription, 8> attributes{{
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(OverlayVertex, position)},
        {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(OverlayVertex, color)},
        {2, 0, VK_FORMAT_R32_SFLOAT, offsetof(OverlayVertex, alpha)},
        {3, 0, VK_FORMAT_R32_SFLOAT, offsetof(OverlayVertex, path_distance)},
        {4, 0, VK_FORMAT_R32_SFLOAT, offsetof(OverlayVertex, dash_direction)},
        {5, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(OverlayVertex, surface_tangent)},
        {6, 0, VK_FORMAT_R32_SFLOAT, offsetof(OverlayVertex, surface_radius)},
        {7, 0, VK_FORMAT_R32_SFLOAT, offsetof(OverlayVertex, screen_offset_pixels)},
    }};
    VkPipelineVertexInputStateCreateInfo vertex_input{};
    vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input.vertexBindingDescriptionCount = 1;
    vertex_input.pVertexBindingDescriptions = &binding;
    vertex_input.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(attributes.size());
    vertex_input.pVertexAttributeDescriptions = attributes.data();

    VkPipelineInputAssemblyStateCreateInfo input_assembly{};
    input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;

    VkPipelineViewportStateCreateInfo viewport_state{};
    viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount = 1;
    viewport_state.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterization{};
    rasterization.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterization.polygonMode = VK_POLYGON_MODE_FILL;
    rasterization.cullMode = VK_CULL_MODE_NONE;
    rasterization.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterization.lineWidth = 1.0F;

    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState blend_attachment{};
    blend_attachment.blendEnable = VK_TRUE;
    blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend_attachment.colorBlendOp = VK_BLEND_OP_ADD;
    blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    blend_attachment.alphaBlendOp = VK_BLEND_OP_ADD;
    blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
                                      VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo color_blend{};
    color_blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blend.attachmentCount = 1;
    color_blend.pAttachments = &blend_attachment;

    std::array<VkDynamicState, 2> dynamic_states{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{};
    dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic.dynamicStateCount = static_cast<std::uint32_t>(dynamic_states.size());
    dynamic.pDynamicStates = dynamic_states.data();

    VkGraphicsPipelineCreateInfo pipeline_info{};
    pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeline_info.stageCount = static_cast<std::uint32_t>(stages.size());
    pipeline_info.pStages = stages.data();
    pipeline_info.pVertexInputState = &vertex_input;
    pipeline_info.pInputAssemblyState = &input_assembly;
    pipeline_info.pViewportState = &viewport_state;
    pipeline_info.pRasterizationState = &rasterization;
    pipeline_info.pMultisampleState = &multisample;
    pipeline_info.pColorBlendState = &color_blend;
    pipeline_info.pDynamicState = &dynamic;
    pipeline_info.layout = overlay.pipeline_layout_;
    pipeline_info.renderPass = overlay.render_pass_;
    pipeline_info.subpass = 0;

    auto pipeline_result = require_vk(vkCreateGraphicsPipelines(overlay.device_, VK_NULL_HANDLE, 1, &pipeline_info,
                                                               nullptr, &overlay.pipeline_),
                                      "vkCreateGraphicsPipelines");
    vkDestroyShaderModule(overlay.device_, *vert, nullptr);
    vkDestroyShaderModule(overlay.device_, *frag, nullptr);
    if (!pipeline_result) {
        overlay.reset();
        return std::unexpected(pipeline_result.error());
    }

    // Host-visible vertex buffer for the guide lines (uploaded per frame).
    overlay.vertex_capacity_ = kMaxLines * 2;
    VkBufferCreateInfo buffer_info{};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = sizeof(OverlayVertex) * overlay.vertex_capacity_;
    buffer_info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (auto result = require_vk(vkCreateBuffer(overlay.device_, &buffer_info, nullptr, &overlay.vertex_buffer_),
                                 "vkCreateBuffer");
        !result) {
        overlay.reset();
        return std::unexpected(result.error());
    }

    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(overlay.device_, overlay.vertex_buffer_, &requirements);
    auto memory_type = find_memory_type(context.physical_device(), requirements.memoryTypeBits,
                                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (!memory_type) {
        overlay.reset();
        return std::unexpected(memory_type.error());
    }
    VkMemoryAllocateInfo allocate_info{};
    allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocate_info.allocationSize = requirements.size;
    allocate_info.memoryTypeIndex = *memory_type;
    if (auto result = require_vk(vkAllocateMemory(overlay.device_, &allocate_info, nullptr, &overlay.vertex_memory_),
                                 "vkAllocateMemory");
        !result) {
        overlay.reset();
        return std::unexpected(result.error());
    }
    vkBindBufferMemory(overlay.device_, overlay.vertex_buffer_, overlay.vertex_memory_, 0);
    if (auto result = require_vk(vkMapMemory(overlay.device_, overlay.vertex_memory_, 0, buffer_info.size, 0,
                                             &overlay.vertex_mapped_),
                                 "vkMapMemory");
        !result) {
        overlay.reset();
        return std::unexpected(result.error());
    }

    return overlay;
}

Result<void> ViewportOverlay::set_target(VkImage target, VkFormat format, VkExtent2D extent)
{
    reset_target();
    VkImageViewCreateInfo view_info{};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image = target;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = format;
    view_info.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (auto result = require_vk(vkCreateImageView(device_, &view_info, nullptr, &target_view_), "vkCreateImageView");
        !result) {
        return std::unexpected(result.error());
    }
    VkFramebufferCreateInfo framebuffer_info{};
    framebuffer_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebuffer_info.renderPass = render_pass_;
    framebuffer_info.attachmentCount = 1;
    framebuffer_info.pAttachments = &target_view_;
    framebuffer_info.width = extent.width;
    framebuffer_info.height = extent.height;
    framebuffer_info.layers = 1;
    return require_vk(vkCreateFramebuffer(device_, &framebuffer_info, nullptr, &framebuffer_), "vkCreateFramebuffer");
}

Result<void> ViewportOverlay::set_scene_distance(std::uint32_t distance_slot, VkImageView distance_view)
{
    if (distance_slot >= kDistanceSlotCount) {
        return std::unexpected(make_error("overlay distance slot out of range"));
    }
    VkDescriptorImageInfo image_info{};
    image_info.sampler = sampler_;
    image_info.imageView = distance_view;
    image_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = descriptor_sets_[distance_slot];
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &image_info;
    vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
    return {};
}

Result<void> ViewportOverlay::record(VkCommandBuffer command_buffer, VkExtent2D extent, VkRect2D content_rect,
                                     const OverlayCamera& camera, std::span<const OverlayLine> lines,
                                     float depth_bias, float animation_time, std::uint32_t distance_slot)
{
    if (distance_slot >= kDistanceSlotCount) {
        return std::unexpected(make_error("overlay distance slot out of range"));
    }
    if (framebuffer_ == VK_NULL_HANDLE) {
        return std::unexpected(make_error("overlay target is missing"));
    }

    if (lines.size() > kMaxLines) {
        return std::unexpected(make_error("overlay line capacity exceeded"));
    }
    const std::uint32_t line_count = static_cast<std::uint32_t>(lines.size());
    auto* vertices = static_cast<OverlayVertex*>(vertex_mapped_);
    for (std::uint32_t index = 0; index < line_count; ++index) {
        const auto& line = lines[index];
        OverlayVertex& start = vertices[index * 2];
        OverlayVertex& end = vertices[index * 2 + 1];
        std::memcpy(start.position, line.start, sizeof(line.start));
        std::memcpy(start.color, line.color, sizeof(line.color));
        start.alpha = line.alpha;
        start.path_distance = 0.0F;
        start.dash_direction = line.dash_direction;
        std::memcpy(start.surface_tangent, line.surface_tangent, sizeof(line.surface_tangent));
        start.surface_radius = line.surface_radius;
        start.screen_offset_pixels = line.screen_offset_pixels;
        std::memcpy(end.position, line.end, sizeof(line.end));
        std::memcpy(end.color, line.color, sizeof(line.color));
        end.alpha = line.alpha;
        const float dx = line.end[0] - line.start[0];
        const float dy = line.end[1] - line.start[1];
        const float dz = line.end[2] - line.start[2];
        end.path_distance = std::sqrt(dx * dx + dy * dy + dz * dz);
        end.dash_direction = line.dash_direction;
        std::memcpy(end.surface_tangent, line.surface_tangent, sizeof(line.surface_tangent));
        end.surface_radius = line.surface_radius;
        end.screen_offset_pixels = line.screen_offset_pixels;
    }

    VkRenderPassBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    begin.renderPass = render_pass_;
    begin.framebuffer = framebuffer_;
    begin.renderArea.extent = extent;
    vkCmdBeginRenderPass(command_buffer, &begin, VK_SUBPASS_CONTENTS_INLINE);

    if (line_count > 0) {
        VkViewport viewport{static_cast<float>(content_rect.offset.x), static_cast<float>(content_rect.offset.y),
                            static_cast<float>(content_rect.extent.width),
                            static_cast<float>(content_rect.extent.height), 0.0F, 1.0F};
        vkCmdSetViewport(command_buffer, 0, 1, &viewport);
        vkCmdSetScissor(command_buffer, 0, 1, &content_rect);
        vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
        vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout_, 0, 1,
                                &descriptor_sets_[distance_slot], 0, nullptr);

        OverlayPushConstants push{};
        std::memcpy(push.eye, camera.eye, sizeof(camera.eye));
        std::memcpy(push.right, camera.right, sizeof(camera.right));
        std::memcpy(push.up, camera.up, sizeof(camera.up));
        std::memcpy(push.negative_forward, camera.negative_forward, sizeof(camera.negative_forward));
        push.projection[0] = camera.focal_length;
        push.projection[1] = camera.horizontal_aperture;
        push.projection[2] = camera.vertical_aperture;
        push.projection[3] = camera.near_clip;
        push.depth[0] = camera.far_clip;
        push.depth[1] = depth_bias;
        push.depth[2] = animation_time;
        push.viewport[0] = static_cast<float>(content_rect.offset.x);
        push.viewport[1] = static_cast<float>(content_rect.offset.y);
        push.viewport[2] = static_cast<float>(content_rect.extent.width);
        push.viewport[3] = static_cast<float>(content_rect.extent.height);
        vkCmdPushConstants(command_buffer, pipeline_layout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                           sizeof(push), &push);

        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(command_buffer, 0, 1, &vertex_buffer_, &offset);
        vkCmdDraw(command_buffer, line_count * 2, 1, 0, 0);
    }

    vkCmdEndRenderPass(command_buffer);
    return {};
}

void ViewportOverlay::reset_target()
{
    if (device_ == VK_NULL_HANDLE) return;
    if (framebuffer_ != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(device_, framebuffer_, nullptr);
        framebuffer_ = VK_NULL_HANDLE;
    }
    if (target_view_ != VK_NULL_HANDLE) {
        vkDestroyImageView(device_, target_view_, nullptr);
        target_view_ = VK_NULL_HANDLE;
    }
}

void ViewportOverlay::reset()
{
    if (device_ == VK_NULL_HANDLE) {
        return;
    }
    reset_target();
    if (vertex_mapped_ != nullptr) {
        vkUnmapMemory(device_, vertex_memory_);
        vertex_mapped_ = nullptr;
    }
    if (vertex_buffer_ != VK_NULL_HANDLE) {
        vkDestroyBuffer(device_, vertex_buffer_, nullptr);
        vertex_buffer_ = VK_NULL_HANDLE;
    }
    if (vertex_memory_ != VK_NULL_HANDLE) {
        vkFreeMemory(device_, vertex_memory_, nullptr);
        vertex_memory_ = VK_NULL_HANDLE;
    }
    if (pipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(device_, pipeline_, nullptr);
        pipeline_ = VK_NULL_HANDLE;
    }
    if (pipeline_layout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device_, pipeline_layout_, nullptr);
        pipeline_layout_ = VK_NULL_HANDLE;
    }
    if (sampler_ != VK_NULL_HANDLE) {
        vkDestroySampler(device_, sampler_, nullptr);
        sampler_ = VK_NULL_HANDLE;
    }
    if (descriptor_pool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device_, descriptor_pool_, nullptr);
        descriptor_pool_ = VK_NULL_HANDLE;
        for (auto& descriptor_set : descriptor_sets_) {
            descriptor_set = VK_NULL_HANDLE;
        }
    }
    if (descriptor_set_layout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device_, descriptor_set_layout_, nullptr);
        descriptor_set_layout_ = VK_NULL_HANDLE;
    }
    if (render_pass_ != VK_NULL_HANDLE) {
        vkDestroyRenderPass(device_, render_pass_, nullptr);
        render_pass_ = VK_NULL_HANDLE;
    }
    device_ = VK_NULL_HANDLE;
}

void ViewportOverlay::move_from(ViewportOverlay& other) noexcept
{
    device_ = std::exchange(other.device_, VK_NULL_HANDLE);
    render_pass_ = std::exchange(other.render_pass_, VK_NULL_HANDLE);
    descriptor_set_layout_ = std::exchange(other.descriptor_set_layout_, VK_NULL_HANDLE);
    descriptor_pool_ = std::exchange(other.descriptor_pool_, VK_NULL_HANDLE);
    for (std::uint32_t slot = 0; slot < kDistanceSlotCount; ++slot) {
        descriptor_sets_[slot] = std::exchange(other.descriptor_sets_[slot], VK_NULL_HANDLE);
    }
    sampler_ = std::exchange(other.sampler_, VK_NULL_HANDLE);
    pipeline_layout_ = std::exchange(other.pipeline_layout_, VK_NULL_HANDLE);
    pipeline_ = std::exchange(other.pipeline_, VK_NULL_HANDLE);
    vertex_buffer_ = std::exchange(other.vertex_buffer_, VK_NULL_HANDLE);
    vertex_memory_ = std::exchange(other.vertex_memory_, VK_NULL_HANDLE);
    vertex_mapped_ = std::exchange(other.vertex_mapped_, nullptr);
    vertex_capacity_ = std::exchange(other.vertex_capacity_, 0);
    target_view_ = std::exchange(other.target_view_, VK_NULL_HANDLE);
    framebuffer_ = std::exchange(other.framebuffer_, VK_NULL_HANDLE);
}

ViewportOverlay::ViewportOverlay(ViewportOverlay&& other) noexcept
{
    move_from(other);
}

ViewportOverlay& ViewportOverlay::operator=(ViewportOverlay&& other) noexcept
{
    if (this != &other) {
        reset();
        move_from(other);
    }
    return *this;
}

ViewportOverlay::~ViewportOverlay()
{
    reset();
}

} // namespace toi::viewport
