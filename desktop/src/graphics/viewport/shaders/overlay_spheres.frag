#version 450

layout(binding = 0) uniform sampler2D scene_distance_to_camera;

layout(push_constant) uniform PushConstants {
    vec4 eye;
    vec4 right;
    vec4 up;
    vec4 negative_forward;
    vec4 projection;
    vec4 depth;    // far_clip, depth_bias, animation_time, unused
    vec4 viewport; // x, y, width, height of the contained rendered image
} pc;

layout(location = 0) flat in vec3 sphere_center;
layout(location = 1) flat in float sphere_radius;
layout(location = 2) flat in vec4 sphere_color;

layout(location = 0) out vec4 out_color;

void main()
{
    vec2 uv = (gl_FragCoord.xy - pc.viewport.xy) / pc.viewport.zw;
    vec2 ndc = uv * 2.0 - 1.0;
    vec3 ray_direction = normalize(
        -pc.negative_forward.xyz +
        pc.right.xyz * ndc.x * pc.projection.y / (2.0 * pc.projection.x) -
        pc.up.xyz * ndc.y * pc.projection.z / (2.0 * pc.projection.x));

    vec3 origin_to_center = pc.eye.xyz - sphere_center;
    float projected_origin = dot(origin_to_center, ray_direction);
    float discriminant = projected_origin * projected_origin -
        (dot(origin_to_center, origin_to_center) - sphere_radius * sphere_radius);
    if (discriminant < 0.0) {
        discard;
    }

    float ray_forward = dot(ray_direction, -pc.negative_forward.xyz);
    float near_distance = pc.projection.w / ray_forward;
    float far_distance = pc.depth.x / ray_forward;
    float discriminant_root = sqrt(discriminant);
    float near_hit = -projected_origin - discriminant_root;
    float far_hit = -projected_origin + discriminant_root;
    float hit_distance = near_hit > near_distance ? near_hit : far_hit;
    if (hit_distance <= near_distance || hit_distance >= far_distance) {
        discard;
    }

    float scene_distance = texture(scene_distance_to_camera, uv).r;
    if (scene_distance > 0.0 && scene_distance < 3.402823e38 &&
        hit_distance > scene_distance + pc.depth.y) {
        discard;
    }

    vec3 hit_position = pc.eye.xyz + ray_direction * hit_distance;
    vec3 normal = normalize(hit_position - sphere_center);
    float facing = clamp(dot(normal, -ray_direction), 0.0, 1.0);
    float rim = pow(1.0 - facing, 3.0);
    float shade = 0.72 + facing * 0.28;
    vec3 color = sphere_color.rgb * shade + rim * 0.12;
    float alpha = min(0.6, sphere_color.a * mix(1.0, 1.9, rim));
    out_color = vec4(color, alpha);
}
