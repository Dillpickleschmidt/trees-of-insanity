#version 450

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_color;
layout(location = 2) in float in_alpha;

layout(push_constant) uniform PushConstants {
    vec4 eye;
    vec4 right;
    vec4 up;
    vec4 negative_forward;
    vec4 projection; // focal_length, horizontal_aperture, vertical_aperture, near_clip
    vec4 depth;      // far_clip, guide_depth_bias, unused, surface_depth_tolerance
    vec4 viewport;   // x, y, width, height of the contained rendered image
} pc;

layout(location = 0) out vec3 fragment_world_position;
layout(location = 1) out vec4 fragment_color;

void main()
{
    vec3 relative = in_position - pc.eye.xyz;
    float forward_distance = -dot(relative, pc.negative_forward.xyz);
    float camera_x = dot(relative, pc.right.xyz);
    float camera_y = dot(relative, pc.up.xyz);

    float focal_length = pc.projection.x;
    float horizontal_aperture = pc.projection.y;
    float vertical_aperture = pc.projection.z;
    float near_clip = pc.projection.w;
    float far_clip = pc.depth.x;

    float clip_x = 2.0 * focal_length * camera_x / horizontal_aperture;
    float clip_y = -2.0 * focal_length * camera_y / vertical_aperture;
    float clip_z = far_clip / (far_clip - near_clip) * forward_distance -
                   (far_clip * near_clip) / (far_clip - near_clip);

    gl_Position = vec4(clip_x, clip_y, clip_z, forward_distance);
    fragment_world_position = in_position;
    fragment_color = vec4(in_color, in_alpha);
}
