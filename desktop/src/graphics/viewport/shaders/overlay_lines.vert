#version 450

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_color;
layout(location = 2) in float in_alpha;
layout(location = 3) in float in_path_distance;
layout(location = 4) in vec3 in_surface_tangent;
layout(location = 5) in float in_surface_radius;
layout(location = 6) in float in_ribbon_side;

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
layout(location = 3) flat out float fragment_path_animation;
layout(location = 4) flat out float fragment_surface_radius;

void main()
{
    vec3 render_position = in_position;
    if (in_surface_radius > 0.0) {
        vec3 tangent = normalize(in_surface_tangent);
        vec3 toward_camera = normalize(pc.eye.xyz - in_position);
        vec3 surface_normal = toward_camera - tangent * dot(toward_camera, tangent);
        if (length(surface_normal) < 0.000001) {
            surface_normal = pc.right.xyz - tangent * dot(pc.right.xyz, tangent);
        }
        if (length(surface_normal) < 0.000001) {
            surface_normal = pc.up.xyz - tangent * dot(pc.up.xyz, tangent);
        }
        surface_normal = normalize(surface_normal);
        vec3 ribbon_direction = normalize(cross(tangent, surface_normal));
        render_position += surface_normal * in_surface_radius * 1.05 +
                           ribbon_direction * in_surface_radius * in_ribbon_side;
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
    float clip_z = far_clip / (far_clip - near_clip) * forward_distance -
                   (far_clip * near_clip) / (far_clip - near_clip);

    gl_Position = vec4(clip_x, clip_y, clip_z, forward_distance);
    fragment_world_position = render_position;
    fragment_color = vec4(in_color, in_alpha);
    fragment_path_distance = in_path_distance;
    fragment_path_animation = abs(in_ribbon_side);
    fragment_surface_radius = in_surface_radius;
}
