#version 450

// The ovrtx DistanceToCameraSD output, shared from CUDA (R32_SFLOAT).
layout(binding = 0) uniform sampler2D scene_distance_to_camera;

layout(push_constant) uniform PushConstants {
    vec4 eye;
    vec4 right;
    vec4 up;
    vec4 negative_forward;
    vec4 projection;
    vec4 depth; // far_clip, depth_bias, framebuffer_width, framebuffer_height
} pc;

layout(location = 0) in vec3 fragment_world_position;
layout(location = 1) in vec4 fragment_color;

layout(location = 0) out vec4 out_color;

void main()
{
    // The scene-distance image is the render resolution; sample by normalized
    // screen coordinate so it maps across the scaled blit.
    vec2 uv = gl_FragCoord.xy / vec2(pc.depth.z, pc.depth.w);
    float scene_distance = texture(scene_distance_to_camera, uv).r;
    float guide_distance = length(fragment_world_position - pc.eye.xyz);
    float depth_bias = pc.depth.y;
    if (scene_distance > 0.0 && scene_distance < 3.402823e38 && guide_distance > scene_distance + depth_bias) {
        discard;
    }

    out_color = fragment_color;
}
