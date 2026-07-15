#version 450

layout(location = 0) in vec3 in_center;
layout(location = 1) in float in_radius;
layout(location = 2) in vec3 in_color;
layout(location = 3) in float in_alpha;

layout(push_constant) uniform PushConstants {
    vec4 eye;
    vec4 right;
    vec4 up;
    vec4 negative_forward;
    vec4 projection; // focal_length, horizontal_aperture, vertical_aperture, near_clip
    vec4 depth;      // far_clip, depth_bias, animation_time, unused
    vec4 viewport;   // x, y, width, height of the contained rendered image
} pc;

layout(location = 0) flat out vec3 sphere_center;
layout(location = 1) flat out float sphere_radius;
layout(location = 2) flat out vec4 sphere_color;

const vec2 corners[6] = vec2[](
    vec2(-1.0, -1.0), vec2( 1.0, -1.0), vec2( 1.0,  1.0),
    vec2(-1.0, -1.0), vec2( 1.0,  1.0), vec2(-1.0,  1.0)
);

vec2 tangent_slopes(float center_axis, float center_forward, float radius)
{
    float denominator = center_forward * center_forward - radius * radius;
    float tangent_offset = radius * sqrt(max(
        center_axis * center_axis + center_forward * center_forward - radius * radius, 0.0));
    return vec2(
        (center_axis * center_forward - tangent_offset) / denominator,
        (center_axis * center_forward + tangent_offset) / denominator);
}

void main()
{
    vec2 corner = corners[gl_VertexIndex];
    vec3 to_center = in_center - pc.eye.xyz;
    float center_forward = -dot(to_center, pc.negative_forward.xyz);
    vec2 ndc_position;
    if (center_forward <= in_radius + pc.projection.w) {
        ndc_position = corner;
    } else {
        float center_x = dot(to_center, pc.right.xyz);
        float center_y = dot(to_center, pc.up.xyz);
        vec2 x_slopes = tangent_slopes(center_x, center_forward, in_radius);
        vec2 y_slopes = tangent_slopes(center_y, center_forward, in_radius);
        float horizontal_scale = 2.0 * pc.projection.x / pc.projection.y;
        float vertical_scale = 2.0 * pc.projection.x / pc.projection.z;
        vec2 ndc_min = vec2(x_slopes.x * horizontal_scale, -y_slopes.y * vertical_scale);
        vec2 ndc_max = vec2(x_slopes.y * horizontal_scale, -y_slopes.x * vertical_scale);
        ndc_position = mix(ndc_min, ndc_max, corner * 0.5 + 0.5);
    }
    gl_Position = vec4(ndc_position, 0.0, 1.0);

    sphere_center = in_center;
    sphere_radius = in_radius;
    sphere_color = vec4(in_color, in_alpha);
}
