#version 450

layout(binding = 0) uniform sampler2D scene_distance_to_camera;

layout(push_constant) uniform PushConstants {
    vec4 eye;
    vec4 right;
    vec4 up;
    vec4 negative_forward;
    vec4 projection;
    vec4 depth;    // far_clip, guide_depth_bias, animation_time, surface_depth_tolerance
    vec4 viewport; // x, y, width, height of the contained rendered image
} pc;

layout(location = 0) in vec3 fragment_world_position;
layout(location = 1) in vec4 fragment_color;
layout(location = 2) in float fragment_distance_from_root;
layout(location = 3) flat in float fragment_animation_direction;

layout(location = 0) out vec4 out_color;

void main()
{
    vec2 uv = (gl_FragCoord.xy - pc.viewport.xy) / pc.viewport.zw;
    float scene_distance = texture(scene_distance_to_camera, uv).r;
    float surface_distance = length(fragment_world_position - pc.eye.xyz);
    if (scene_distance > 0.0 && scene_distance < 3.402823e38 &&
        surface_distance > scene_distance + pc.depth.w) {
        discard;
    }

    float phase = fragment_distance_from_root * 24.0 -
                  fragment_animation_direction * pc.depth.z * 2.0;
    if (fract(phase) > 0.55) {
        discard;
    }
    out_color = fragment_color;
}
