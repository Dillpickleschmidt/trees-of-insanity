#version 450

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_color;
layout(location = 2) in float in_alpha;
layout(location = 3) in float in_path_distance;
layout(location = 4) in float in_dash_direction;
layout(location = 5) in vec3 in_surface_tangent;
layout(location = 6) in float in_surface_radius;
layout(location = 7) in float in_screen_offset_pixels;

// Matches the ovrtx physical camera so guide lines align with the rendered
// scene: focal length + apertures define the projection.
layout(push_constant) uniform PushConstants {
    vec4 eye;
    vec4 right;
    vec4 up;
    vec4 negative_forward;
    vec4 projection; // focal_length, horizontal_aperture, vertical_aperture, near_clip
    vec4 depth;      // far_clip, depth_bias, unused, unused
    vec4 viewport;   // x, y, width, height of the contained rendered image
} pc;

layout(location = 0) out vec3 fragment_world_position;
layout(location = 1) out vec4 fragment_color;
layout(location = 2) out float fragment_path_distance;
layout(location = 3) flat out float fragment_dash_direction;

void main()
{
    vec3 render_position = in_position;
    vec3 tangent = normalize(in_surface_tangent);
    if (in_surface_radius > 0.0) {
        vec3 toward_camera = normalize(pc.eye.xyz - in_position);
        vec3 surface_normal = toward_camera - tangent * dot(toward_camera, tangent);
        if (length(surface_normal) < 0.000001) {
            surface_normal = pc.right.xyz - tangent * dot(pc.right.xyz, tangent);
        }
        if (length(surface_normal) < 0.000001) {
            surface_normal = pc.up.xyz - tangent * dot(pc.up.xyz, tangent);
        }
        render_position += normalize(surface_normal) * in_surface_radius * 1.05;
    }

    vec3 relative = render_position - pc.eye.xyz;
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
    if (in_screen_offset_pixels != 0.0) {
        vec2 screen_tangent = vec2(dot(tangent, pc.right.xyz), -dot(tangent, pc.up.xyz));
        if (length(screen_tangent) < 0.000001) {
            screen_tangent = vec2(1.0, 0.0);
        } else {
            screen_tangent = normalize(screen_tangent);
        }
        vec2 screen_normal = vec2(-screen_tangent.y, screen_tangent.x);
        clip_x += screen_normal.x * in_screen_offset_pixels * 2.0 * forward_distance / pc.viewport.z;
        clip_y += screen_normal.y * in_screen_offset_pixels * 2.0 * forward_distance / pc.viewport.w;
    }
    float clip_z = far_clip / (far_clip - near_clip) * forward_distance -
                   (far_clip * near_clip) / (far_clip - near_clip);

    gl_Position = vec4(clip_x, clip_y, clip_z, forward_distance);
    fragment_world_position = render_position;
    fragment_color = vec4(in_color, in_alpha);
    fragment_path_distance = in_path_distance;
    fragment_dash_direction = in_dash_direction;
}
