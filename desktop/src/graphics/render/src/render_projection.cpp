#include "toi/render/render_projection.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace toi::render {
namespace {

struct Bounds;
struct Vec2;
struct TubeFrame;
struct ContinuationTopology;
struct ChainBuildRequest;
struct MeshGeometry;

[[nodiscard]] growth::Vec3 safe_normalize(growth::Vec3 value, growth::Vec3 fallback);
[[nodiscard]] growth::Vec3 subtract_projection(growth::Vec3 value, growth::Vec3 axis);
[[nodiscard]] growth::Vec3 fallback_normal_for(growth::Vec3 tangent);
[[nodiscard]] growth::Vec3 weight_map_color(float weight);
[[nodiscard]] GrowthPreviewStageProjection make_growth_preview_stage_projection_impl(
    const growth::GrowthSnapshot& snapshot, const growth::GrowthSnapshot& camera_snapshot,
    const growth::BranchModulePrototype& prepared_prototype, GrowthPreviewStageOptions options);
[[nodiscard]] GrowthPreviewMeshStats stats_for(const std::vector<MeshGeometry>& meshes, std::size_t chain_count);
[[nodiscard]] GrowthPreviewCamera make_growth_preview_camera(const growth::GrowthSnapshot& camera_snapshot, int width,
                                                             int height);
void append_collision_sphere_lines(std::vector<DiagnosticOverlayLine>& lines, const growth::Sphere& sphere);
void append_terminal_marker_lines(std::vector<DiagnosticOverlayLine>& lines,
                                  const growth::MatureTerminalSnapshot& terminal);
[[nodiscard]] std::vector<ChainBuildRequest> make_plant_chain_build_requests(
    const growth::PlantSnapshot& snapshot, bool mature_geometry);
[[nodiscard]] std::vector<ChainBuildRequest> make_plant_dynamic_chain_build_requests(
    const growth::PlantSnapshot& snapshot, const std::vector<ChainBuildRequest>& topology_requests);
[[nodiscard]] GrowthPreviewUsdStage make_growth_preview_usd_stage(const std::vector<MeshGeometry>& meshes,
                                                                  const GrowthPreviewCamera& camera,
                                                                  GrowthPreviewStageOptions options);
[[nodiscard]] std::vector<ChainBuildRequest>
make_chain_build_requests(const growth::GrowthSnapshot& snapshot,
                          const growth::BranchModulePrototype& prepared_prototype);
[[nodiscard]] std::vector<ChainBuildRequest> make_dynamic_chain_build_requests(
    const std::vector<ChainBuildRequest>& topology_requests, const growth::GrowthSnapshot& snapshot,
    const growth::GrowthSnapshot& topology_snapshot, const growth::BranchModulePrototype& prepared_prototype);
[[nodiscard]] std::vector<MeshGeometry> build_meshes(const std::vector<ChainBuildRequest>& requests);
[[nodiscard]] std::vector<GrowthPreviewMeshAttributes> mesh_attributes_for(const std::vector<MeshGeometry>& meshes);
[[nodiscard]] GrowthPreviewCamera make_camera_from_bounds(Bounds bounds, int requested_width, int requested_height);

} // namespace

GrowthPreviewStageProjection make_growth_preview_stage_projection(
    const growth::GrowthSnapshot& snapshot, const growth::GrowthSnapshot& camera_snapshot,
    const growth::BranchModulePrototype& prepared_prototype, GrowthPreviewStageOptions options)
{
    return make_growth_preview_stage_projection_impl(snapshot, camera_snapshot, prepared_prototype, options);
}

GrowthPreviewStageProjection make_plant_preview_stage_projection(
    const growth::PlantSnapshot& snapshot, const growth::GrowthSnapshot& mature_root_snapshot,
    PlantDiagnosticOptions diagnostics, GrowthPreviewStageOptions options)
{
    auto topology_requests = make_plant_chain_build_requests(snapshot, true);
    auto topology_meshes = build_meshes(topology_requests);
    auto dynamic_requests = make_plant_dynamic_chain_build_requests(snapshot, topology_requests);
    auto dynamic_meshes = build_meshes(dynamic_requests);
    const auto camera = make_growth_preview_camera(mature_root_snapshot, options.width, options.height);
    auto projection = GrowthPreviewStageProjection{
        .usd_stage = make_growth_preview_usd_stage(topology_meshes, camera, options),
        .mesh = stats_for(dynamic_meshes, dynamic_requests.size()),
        .camera = camera,
        .mesh_attributes = mesh_attributes_for(dynamic_meshes),
    };

    for (const auto& module : snapshot.modules) {
        if (diagnostics.show_collision_spheres && module.collision_sphere.radius > 0.0F) {
            append_collision_sphere_lines(projection.diagnostic_lines, module.collision_sphere);
        }
        if (diagnostics.show_labels) {
            projection.diagnostic_labels.push_back({
                .world_position = module.root_position,
                .direct_light_exposure = module.direct_light_exposure,
                .accumulated_light = module.accumulated_light,
                .vigor = module.vigor,
            });
        }
    }
    for (const auto& flow : snapshot.flow_paths) {
        if ((flow.kind == growth::FlowKind::AccumulatedLight && !diagnostics.show_accumulated_light_flow) ||
            (flow.kind == growth::FlowKind::Vigor && !diagnostics.show_vigor_flow)) {
            continue;
        }
        projection.diagnostic_paths.push_back({
            .start = flow.start,
            .end = flow.end,
            .color = weight_map_color(flow.fraction),
            .host_radius = flow.host_radius,
        });
    }
    if (diagnostics.show_mature_terminals) {
        for (const auto& terminal : snapshot.mature_terminals) {
            append_terminal_marker_lines(projection.diagnostic_lines, terminal);
        }
    }
    return projection;
}

std::array<double, 16> growth_preview_camera_transform_matrix(const GrowthPreviewCamera& camera)
{
    return {
        camera.right.x,
        camera.right.y,
        camera.right.z,
        0.0,
        camera.up.x,
        camera.up.y,
        camera.up.z,
        0.0,
        camera.negative_forward.x,
        camera.negative_forward.y,
        camera.negative_forward.z,
        0.0,
        camera.eye.x,
        camera.eye.y,
        camera.eye.z,
        1.0,
    };
}

namespace {

struct Bounds {
    growth::Vec3 min;
    growth::Vec3 max;
};

struct Vec2 {
    float x = 0.0F;
    float y = 0.0F;
};

struct TubeFrame {
    growth::Vec3 tangent;
    growth::Vec3 normal;
    growth::Vec3 binormal;
};

struct ContinuationTopology {
    std::vector<std::optional<std::size_t>> continuation_by_segment;
};

struct ChainBuildRequest {
    std::vector<std::size_t> source_segment_ids;
    std::vector<growth::Vec3> centers;
    std::vector<float> radii;
    bool start_cap = false;
    bool end_cap = true;
};

struct MeshGeometry {
    std::vector<growth::Vec3> points;
    std::vector<std::int32_t> face_vertex_counts;
    std::vector<std::int32_t> face_vertex_indices;
    std::vector<Vec2> st;
    std::vector<std::int32_t> face_source_segment_ids;
};

constexpr std::string_view kWorldPrimName = "World";
constexpr std::string_view kGrowthPreviewPrimName = "GrowthPreview";
constexpr std::string_view kChainsPrimName = "Chains";
constexpr std::string_view kCameraPath = "/OVCamera";
constexpr std::string_view kRenderProductPath = "/OVRenderProduct";
constexpr std::string_view kRenderSettingsPath = "/OVRenderSettings";
constexpr int kRadialSegmentCount = 10;
constexpr float kPi = 3.14159265358979323846F;
constexpr float kTwoPi = 2.0F * kPi;
constexpr float kEpsilon = 1.0e-6F;
constexpr double kCameraFocalLength = 18.15;
constexpr double kCameraHorizontalAperture = 20.955;

GrowthPreviewStageProjection make_growth_preview_stage_projection_impl(
    const growth::GrowthSnapshot& snapshot, const growth::GrowthSnapshot& camera_snapshot,
    const growth::BranchModulePrototype& prepared_prototype, GrowthPreviewStageOptions options)
{
    auto topology_requests = make_chain_build_requests(camera_snapshot, prepared_prototype);
    auto topology_meshes = build_meshes(topology_requests);
    auto dynamic_requests =
        make_dynamic_chain_build_requests(topology_requests, snapshot, camera_snapshot, prepared_prototype);
    auto dynamic_meshes = build_meshes(dynamic_requests);
    auto active_requests = make_chain_build_requests(snapshot, prepared_prototype);
    auto active_meshes = build_meshes(active_requests);
    const auto camera = make_growth_preview_camera(camera_snapshot, options.width, options.height);
    auto usd_stage = make_growth_preview_usd_stage(topology_meshes, camera, options);

    return {
        .usd_stage = std::move(usd_stage),
        .mesh = stats_for(active_meshes, active_requests.size()),
        .camera = camera,
        .mesh_attributes = mesh_attributes_for(dynamic_meshes),
    };
}

[[nodiscard]] bool is_finite(growth::Vec3 value)
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

[[nodiscard]] bool is_renderable(const growth::GrowthSnapshotSegment& segment)
{
    return is_finite(segment.parent_position) && is_finite(segment.child_position) &&
           growth::distance(segment.parent_position, segment.child_position) > kEpsilon &&
           std::isfinite(segment.diameter) && segment.diameter > kEpsilon;
}

[[nodiscard]] float dot(growth::Vec3 left, growth::Vec3 right)
{
    return left.x * right.x + left.y * right.y + left.z * right.z;
}

[[nodiscard]] growth::Vec3 cross(growth::Vec3 left, growth::Vec3 right)
{
    return {
        .x = left.y * right.z - left.z * right.y,
        .y = left.z * right.x - left.x * right.z,
        .z = left.x * right.y - left.y * right.x,
    };
}

[[nodiscard]] growth::Vec3 subtract_projection(growth::Vec3 value, growth::Vec3 axis)
{
    return growth::subtract(value, growth::scale(axis, dot(value, axis)));
}

[[nodiscard]] growth::Vec3 safe_normalize(growth::Vec3 value, growth::Vec3 fallback)
{
    return growth::length(value) <= kEpsilon ? fallback : growth::normalize(value);
}

[[nodiscard]] growth::Vec3 fallback_normal_for(growth::Vec3 tangent)
{
    const growth::Vec3 z_axis{.x = 0.0F, .y = 0.0F, .z = 1.0F};
    const growth::Vec3 x_axis{.x = 1.0F, .y = 0.0F, .z = 0.0F};
    const growth::Vec3 reference = std::abs(dot(tangent, z_axis)) > 0.9F ? x_axis : z_axis;
    return safe_normalize(cross(reference, tangent), {.x = 1.0F, .y = 0.0F, .z = 0.0F});
}

[[nodiscard]] growth::Vec3 weight_map_color(float weight)
{
    weight = std::clamp(weight, 0.0F, 1.0F);
    if (weight <= 0.25F) {
        return {.x = 0.0F, .y = weight * 4.0F, .z = 1.0F};
    }
    if (weight <= 0.5F) {
        const float interpolation = (weight - 0.25F) * 4.0F;
        return {.x = 0.0F, .y = 1.0F, .z = 1.0F - interpolation};
    }
    if (weight <= 0.75F) {
        const float interpolation = (weight - 0.5F) * 4.0F;
        return {.x = interpolation, .y = 1.0F, .z = 0.0F};
    }
    const float interpolation = (weight - 0.75F) * 4.0F;
    return {.x = 1.0F, .y = 1.0F - interpolation, .z = 0.0F};
}

[[nodiscard]] growth::Vec3 ring_direction(const TubeFrame& frame, int radial_index)
{
    const float angle = kTwoPi * static_cast<float>(radial_index) / static_cast<float>(kRadialSegmentCount);
    return growth::add(growth::scale(frame.normal, std::cos(angle)), growth::scale(frame.binormal, std::sin(angle)));
}

[[nodiscard]] Vec2 disk_uv(int radial_index)
{
    const float angle = kTwoPi * static_cast<float>(radial_index) / static_cast<float>(kRadialSegmentCount);
    return {.x = 0.5F + std::cos(angle) * 0.5F, .y = 0.5F + std::sin(angle) * 0.5F};
}

[[nodiscard]] std::int32_t ring_vertex_index(std::size_t ring_index, int radial_index)
{
    return static_cast<std::int32_t>(ring_index * kRadialSegmentCount + radial_index);
}

[[nodiscard]] Bounds snapshot_bounds(const growth::GrowthSnapshot& snapshot)
{
    if (snapshot.segments.empty()) {
        return {
            .min = {.x = -1.0F, .y = -1.0F, .z = 0.0F},
            .max = {.x = 1.0F, .y = 1.0F, .z = 1.0F},
        };
    }

    Bounds bounds{
        .min = snapshot.segments.front().parent_position,
        .max = snapshot.segments.front().parent_position,
    };

    const auto include = [&bounds](growth::Vec3 point) {
        bounds.min.x = std::min(bounds.min.x, point.x);
        bounds.min.y = std::min(bounds.min.y, point.y);
        bounds.min.z = std::min(bounds.min.z, point.z);
        bounds.max.x = std::max(bounds.max.x, point.x);
        bounds.max.y = std::max(bounds.max.y, point.y);
        bounds.max.z = std::max(bounds.max.z, point.z);
    };

    for (const auto& segment : snapshot.segments) {
        include(segment.parent_position);
        include(segment.child_position);
    }
    return bounds;
}

[[nodiscard]] Bounds mesh_bounds(const MeshGeometry& mesh)
{
    if (mesh.points.empty()) {
        return {
            .min = {.x = 0.0F, .y = 0.0F, .z = 0.0F},
            .max = {.x = 0.0F, .y = 0.0F, .z = 0.0F},
        };
    }

    Bounds bounds{.min = mesh.points.front(), .max = mesh.points.front()};
    for (const auto point : mesh.points) {
        bounds.min.x = std::min(bounds.min.x, point.x);
        bounds.min.y = std::min(bounds.min.y, point.y);
        bounds.min.z = std::min(bounds.min.z, point.z);
        bounds.max.x = std::max(bounds.max.x, point.x);
        bounds.max.y = std::max(bounds.max.y, point.y);
        bounds.max.z = std::max(bounds.max.z, point.z);
    }
    return bounds;
}

[[nodiscard]] growth::Vec3 center_of(Bounds bounds)
{
    return growth::scale(growth::add(bounds.min, bounds.max), 0.5F);
}

[[nodiscard]] float radius_of(Bounds bounds)
{
    return std::max(kEpsilon, growth::length(growth::scale(growth::subtract(bounds.max, bounds.min), 0.5F)));
}

[[nodiscard]] GrowthPreviewCamera make_camera_from_bounds(Bounds bounds, int requested_width, int requested_height)
{
    const int width = std::max(1, requested_width);
    const int height = std::max(1, requested_height);
    const double vertical_aperture =
        kCameraHorizontalAperture * static_cast<double>(height) / static_cast<double>(width);
    const growth::Vec3 center = center_of(bounds);
    const float radius = radius_of(bounds);
    const float distance = radius * 3.0F;
    const growth::Vec3 eye = growth::add(center, {.x = 0.0F, .y = -distance, .z = radius * 0.35F});
    const growth::Vec3 reference_up{.x = 0.0F, .y = 0.0F, .z = 1.0F};
    const growth::Vec3 forward = safe_normalize(growth::subtract(center, eye), {.x = 0.0F, .y = 1.0F, .z = 0.0F});
    const growth::Vec3 right = safe_normalize(cross(forward, reference_up), {.x = 1.0F, .y = 0.0F, .z = 0.0F});
    const growth::Vec3 up = safe_normalize(cross(right, forward), reference_up);
    return {
        .eye = eye,
        .target = center,
        .right = right,
        .up = up,
        .negative_forward = growth::scale(forward, -1.0F),
        .focal_length = kCameraFocalLength,
        .horizontal_aperture = kCameraHorizontalAperture,
        .vertical_aperture = vertical_aperture,
        .width = width,
        .height = height,
    };
}

[[nodiscard]] GrowthPreviewCamera make_growth_preview_camera(const growth::GrowthSnapshot& camera_snapshot,
                                                             int requested_width, int requested_height)
{
    return make_camera_from_bounds(snapshot_bounds(camera_snapshot), requested_width, requested_height);
}

void append_collision_sphere_lines(std::vector<DiagnosticOverlayLine>& lines, const growth::Sphere& sphere)
{
    constexpr int segment_count = 32;
    const std::array planes{
        std::pair{growth::Vec3{1.0F, 0.0F, 0.0F}, growth::Vec3{0.0F, 1.0F, 0.0F}},
        std::pair{growth::Vec3{1.0F, 0.0F, 0.0F}, growth::Vec3{0.0F, 0.0F, 1.0F}},
        std::pair{growth::Vec3{0.0F, 1.0F, 0.0F}, growth::Vec3{0.0F, 0.0F, 1.0F}},
    };
    const auto point_at = [&](growth::Vec3 first_axis, growth::Vec3 second_axis, float angle) {
        const auto radial = growth::add(growth::scale(first_axis, std::cos(angle)),
                                        growth::scale(second_axis, std::sin(angle)));
        return growth::add(sphere.center, growth::scale(radial, sphere.radius));
    };
    lines.reserve(lines.size() + planes.size() * segment_count);
    for (const auto& [first_axis, second_axis] : planes) {
        for (int index = 0; index < segment_count; ++index) {
            const float start_angle = kTwoPi * static_cast<float>(index) / static_cast<float>(segment_count);
            const float end_angle = kTwoPi * static_cast<float>(index + 1) / static_cast<float>(segment_count);
            lines.push_back({
                .start = point_at(first_axis, second_axis, start_angle),
                .end = point_at(first_axis, second_axis, end_angle),
                .color = {.x = 0.15F, .y = 0.85F, .z = 1.0F},
                .alpha = 0.7F,
            });
        }
    }
}

void append_terminal_marker_lines(std::vector<DiagnosticOverlayLine>& lines,
                                  const growth::MatureTerminalSnapshot& terminal)
{
    constexpr int segment_count = 12;
    const float marker_radius = std::max(0.005F, terminal.host_radius * 1.5F);
    const auto tangent = safe_normalize(terminal.tangent, {.x = 0.0F, .y = 0.0F, .z = 1.0F});
    const auto first_axis = fallback_normal_for(tangent);
    const auto second_axis = safe_normalize(cross(tangent, first_axis), {.x = 0.0F, .y = 1.0F, .z = 0.0F});
    const growth::Vec3 color = terminal.axis_role == growth::TerminalAxisRole::Main
        ? growth::Vec3{.x = 0.15F, .y = 1.0F, .z = 0.35F}
        : growth::Vec3{.x = 0.85F, .y = 0.3F, .z = 1.0F};
    const auto point_at = [&](float angle) {
        return growth::add(terminal.position,
                           growth::scale(growth::add(growth::scale(first_axis, std::cos(angle)),
                                                     growth::scale(second_axis, std::sin(angle))),
                                         marker_radius));
    };
    for (int index = 0; index < segment_count; ++index) {
        lines.push_back({
            .start = point_at(kTwoPi * static_cast<float>(index) / segment_count),
            .end = point_at(kTwoPi * static_cast<float>(index + 1) / segment_count),
            .color = color,
            .alpha = 0.9F,
        });
    }
    if (terminal.child_module_id) {
        for (int row = -3; row <= 3; ++row) {
            const float second_offset = static_cast<float>(row) * marker_radius / 4.0F;
            const float half_width = std::sqrt(marker_radius * marker_radius - second_offset * second_offset);
            const auto center = growth::add(terminal.position, growth::scale(second_axis, second_offset));
            lines.push_back({.start = growth::subtract(center, growth::scale(first_axis, half_width)),
                             .end = growth::add(center, growth::scale(first_axis, half_width)),
                             .color = color,
                             .alpha = 0.9F});
        }
    }
}

void write_vec3(std::ostream& out, growth::Vec3 value)
{
    out << "(" << value.x << ", " << value.y << ", " << value.z << ")";
}

void write_vec2(std::ostream& out, Vec2 value)
{
    out << "(" << value.x << ", " << value.y << ")";
}

void write_camera_matrix(std::ostream& out, const GrowthPreviewCamera& camera)
{
    const auto matrix = growth_preview_camera_transform_matrix(camera);
    out << "((" << matrix[0] << ", " << matrix[1] << ", " << matrix[2] << ", " << matrix[3] << "), ";
    out << "(" << matrix[4] << ", " << matrix[5] << ", " << matrix[6] << ", " << matrix[7] << "), ";
    out << "(" << matrix[8] << ", " << matrix[9] << ", " << matrix[10] << ", " << matrix[11] << "), ";
    out << "(" << matrix[12] << ", " << matrix[13] << ", " << matrix[14] << ", " << matrix[15] << "))";
}

void write_int_list(std::ostream& out, const std::vector<std::int32_t>& values)
{
    out << "[";
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index > 0) {
            out << ", ";
        }
        out << values[index];
    }
    out << "]";
}

void write_vec3_list(std::ostream& out, const std::vector<growth::Vec3>& values)
{
    out << "[";
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index > 0) {
            out << ", ";
        }
        write_vec3(out, values[index]);
    }
    out << "]";
}

void write_vec2_list(std::ostream& out, const std::vector<Vec2>& values)
{
    out << "[";
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index > 0) {
            out << ", ";
        }
        write_vec2(out, values[index]);
    }
    out << "]";
}

[[nodiscard]] std::string prim_name_from_path(std::string_view path)
{
    const auto slash = path.find_last_of('/');
    const auto name = slash == std::string_view::npos ? path : path.substr(slash + 1);
    return name.empty() ? "Prim" : std::string(name);
}

[[nodiscard]] bool first_segment_is_better_continuation(const growth::BranchModulePrototype& prototype,
                                                        std::size_t incoming_segment_id, std::size_t left_segment_id,
                                                        std::size_t right_segment_id)
{
    const auto& incoming = prototype.segments[incoming_segment_id];
    const auto& left = prototype.segments[left_segment_id];
    const auto& right = prototype.segments[right_segment_id];
    const float left_dot = dot(incoming.direction, left.direction);
    const float right_dot = dot(incoming.direction, right.direction);
    if (std::abs(left_dot - right_dot) > kEpsilon) {
        return left_dot > right_dot;
    }
    if (std::abs(left.pipe_diameter_factor - right.pipe_diameter_factor) > kEpsilon) {
        return left.pipe_diameter_factor > right.pipe_diameter_factor;
    }
    if (std::abs(left.max_length - right.max_length) > kEpsilon) {
        return left.max_length > right.max_length;
    }
    return left_segment_id < right_segment_id;
}

[[nodiscard]] ContinuationTopology choose_continuations(const growth::BranchModulePrototype& prototype)
{
    ContinuationTopology topology;
    topology.continuation_by_segment.resize(prototype.segments.size());

    for (std::size_t segment_id = 0; segment_id < prototype.segments.size(); ++segment_id) {
        const auto& segment = prototype.segments[segment_id];
        if (segment.child_node >= prototype.child_segments_by_node.size()) {
            continue;
        }

        const auto& child_segments = prototype.child_segments_by_node[segment.child_node];
        if (child_segments.empty()) {
            continue;
        }

        std::size_t best_child_segment = child_segments.front();
        for (const auto child_segment : child_segments) {
            if (first_segment_is_better_continuation(prototype, segment_id, child_segment, best_child_segment)) {
                best_child_segment = child_segment;
            }
        }
        topology.continuation_by_segment[segment_id] = best_child_segment;
    }

    return topology;
}

[[nodiscard]] std::vector<const growth::GrowthSnapshotSegment*>
live_segments_by_source(const growth::GrowthSnapshot& snapshot, const growth::BranchModulePrototype& prototype)
{
    std::vector<const growth::GrowthSnapshotSegment*> live(prototype.segments.size(), nullptr);
    for (const auto& segment : snapshot.segments) {
        if (segment.source_segment_id < live.size() && is_renderable(segment)) {
            live[segment.source_segment_id] = &segment;
        }
    }
    return live;
}

[[nodiscard]] bool has_active_incoming_continuation(const growth::BranchModulePrototype& prototype,
                                                    const ContinuationTopology& topology,
                                                    const std::vector<const growth::GrowthSnapshotSegment*>& live,
                                                    std::size_t segment_id)
{
    const auto& segment = prototype.segments[segment_id];
    if (segment.parent_node >= prototype.incoming_segment_by_node.size()) {
        return false;
    }

    const auto incoming = prototype.incoming_segment_by_node[segment.parent_node];
    if (!incoming.has_value() || *incoming >= live.size() || live[*incoming] == nullptr) {
        return false;
    }
    if (*incoming >= topology.continuation_by_segment.size()) {
        return false;
    }

    return topology.continuation_by_segment[*incoming] == segment_id;
}

[[nodiscard]] ChainBuildRequest make_chain_build_request(const growth::BranchModulePrototype& prototype,
                                                         const ContinuationTopology& topology,
                                                         const std::vector<const growth::GrowthSnapshotSegment*>& live,
                                                         std::size_t first_segment_id)
{
    ChainBuildRequest request;
    request.start_cap = prototype.segments[first_segment_id].parent_node == prototype.root_node;

    const auto* first_segment = live[first_segment_id];
    request.source_segment_ids.push_back(first_segment_id);
    request.centers.push_back(first_segment->parent_position);
    request.radii.push_back(first_segment->diameter * 0.5F);
    request.centers.push_back(first_segment->child_position);
    request.radii.push_back(first_segment->diameter * 0.5F);

    std::size_t current_segment_id = first_segment_id;
    while (current_segment_id < topology.continuation_by_segment.size()) {
        const auto next_continuation = topology.continuation_by_segment[current_segment_id];
        if (!next_continuation) {
            break;
        }
        const std::size_t next_segment_id = *next_continuation;
        if (next_segment_id >= live.size() || live[next_segment_id] == nullptr) {
            break;
        }

        const auto* next_segment = live[next_segment_id];
        request.source_segment_ids.push_back(next_segment_id);
        request.centers.push_back(next_segment->child_position);
        request.radii.push_back(next_segment->diameter * 0.5F);
        current_segment_id = next_segment_id;
    }

    return request;
}

std::vector<ChainBuildRequest> make_chain_build_requests(const growth::GrowthSnapshot& snapshot,
                                                         const growth::BranchModulePrototype& prepared_prototype)
{
    const auto topology = choose_continuations(prepared_prototype);
    const auto live = live_segments_by_source(snapshot, prepared_prototype);

    std::vector<ChainBuildRequest> requests;
    requests.reserve(snapshot.segments.size());
    for (std::size_t segment_id = 0; segment_id < live.size(); ++segment_id) {
        if (live[segment_id] == nullptr) {
            continue;
        }
        if (has_active_incoming_continuation(prepared_prototype, topology, live, segment_id)) {
            continue;
        }
        requests.push_back(make_chain_build_request(prepared_prototype, topology, live, segment_id));
    }
    return requests;
}

[[nodiscard]] growth::Vec3 fallback_parent_position(const growth::BranchModulePrototype& prototype,
                                                    const std::vector<const growth::GrowthSnapshotSegment*>& topology,
                                                    std::size_t segment_id)
{
    if (segment_id < topology.size() && topology[segment_id] != nullptr) {
        return topology[segment_id]->parent_position;
    }
    if (segment_id < prototype.segments.size()) {
        const auto parent_node = prototype.segments[segment_id].parent_node;
        if (parent_node < prototype.nodes.size()) {
            return prototype.nodes[parent_node].position;
        }
    }
    return {};
}

[[nodiscard]] ChainBuildRequest
make_dynamic_chain_build_request(const ChainBuildRequest& topology_request,
                                 const std::vector<const growth::GrowthSnapshotSegment*>& live,
                                 const std::vector<const growth::GrowthSnapshotSegment*>& topology,
                                 const growth::BranchModulePrototype& prepared_prototype)
{
    ChainBuildRequest request;
    request.source_segment_ids = topology_request.source_segment_ids;
    request.start_cap = topology_request.start_cap;
    request.end_cap = topology_request.end_cap;
    if (request.source_segment_ids.empty()) {
        return request;
    }

    const std::size_t first_segment_id = request.source_segment_ids.front();
    const auto* first_segment = first_segment_id < live.size() ? live[first_segment_id] : nullptr;
    request.centers.push_back(first_segment == nullptr
                                  ? fallback_parent_position(prepared_prototype, topology, first_segment_id)
                                  : first_segment->parent_position);
    request.radii.push_back(first_segment == nullptr ? 0.0F : first_segment->diameter * 0.5F);

    for (const std::size_t segment_id : request.source_segment_ids) {
        const auto* segment = segment_id < live.size() ? live[segment_id] : nullptr;
        if (segment == nullptr) {
            request.centers.push_back(request.centers.back());
            request.radii.push_back(0.0F);
            continue;
        }
        request.centers.push_back(segment->child_position);
        request.radii.push_back(segment->diameter * 0.5F);
    }
    return request;
}

std::vector<ChainBuildRequest> make_dynamic_chain_build_requests(
    const std::vector<ChainBuildRequest>& topology_requests, const growth::GrowthSnapshot& snapshot,
    const growth::GrowthSnapshot& topology_snapshot, const growth::BranchModulePrototype& prepared_prototype)
{
    const auto live = live_segments_by_source(snapshot, prepared_prototype);
    const auto topology = live_segments_by_source(topology_snapshot, prepared_prototype);

    std::vector<ChainBuildRequest> requests;
    requests.reserve(topology_requests.size());
    for (const auto& topology_request : topology_requests) {
        requests.push_back(make_dynamic_chain_build_request(topology_request, live, topology, prepared_prototype));
    }
    return requests;
}

std::vector<ChainBuildRequest> make_plant_chain_build_requests(const growth::PlantSnapshot& snapshot,
                                                               bool mature_geometry)
{
    std::vector<ChainBuildRequest> requests;
    for (const auto& module : snapshot.modules) {
        std::vector<bool> is_continuation(module.segments.count, false);
        for (std::size_t local = 0; local < module.segments.count; ++local) {
            const auto& segment = snapshot.segments[module.segments.offset + local];
            if (segment.main_continuation_segment &&
                *segment.main_continuation_segment >= module.segments.offset &&
                *segment.main_continuation_segment < module.segments.offset + module.segments.count) {
                is_continuation[*segment.main_continuation_segment - module.segments.offset] = true;
            }
        }
        for (std::size_t local = 0; local < module.segments.count; ++local) {
            if (is_continuation[local]) continue;
            std::size_t segment_index = module.segments.offset + local;
            ChainBuildRequest request;
            request.start_cap = local == 0;
            const auto& first = snapshot.segments[segment_index];
            request.centers.push_back(mature_geometry ? first.mature_parent_position : first.parent_position);
            request.radii.push_back((mature_geometry ? first.target_diameter : first.diameter) * 0.5F);
            while (true) {
                const auto& segment = snapshot.segments[segment_index];
                request.source_segment_ids.push_back(segment_index);
                request.centers.push_back(mature_geometry ? segment.mature_child_position : segment.child_position);
                const bool developed = growth::distance(segment.parent_position, segment.child_position) > kEpsilon;
                request.radii.push_back(mature_geometry ? segment.target_diameter * 0.5F
                                                        : (developed ? segment.diameter * 0.5F : 0.0F));
                if (!segment.main_continuation_segment) break;
                segment_index = *segment.main_continuation_segment;
            }
            requests.push_back(std::move(request));
        }
    }
    return requests;
}

std::vector<ChainBuildRequest> make_plant_dynamic_chain_build_requests(
    const growth::PlantSnapshot& snapshot, const std::vector<ChainBuildRequest>& topology_requests)
{
    std::vector<ChainBuildRequest> requests;
    requests.reserve(topology_requests.size());
    for (const auto& topology : topology_requests) {
        ChainBuildRequest request;
        request.source_segment_ids = topology.source_segment_ids;
        request.start_cap = topology.start_cap;
        request.end_cap = topology.end_cap;
        const auto& first = snapshot.segments[request.source_segment_ids.front()];
        request.centers.push_back(first.parent_position);
        const bool first_developed = growth::distance(first.parent_position, first.child_position) > kEpsilon;
        request.radii.push_back(first_developed ? first.diameter * 0.5F : 0.0F);
        for (const auto segment_index : request.source_segment_ids) {
            const auto& segment = snapshot.segments[segment_index];
            const bool developed = growth::distance(segment.parent_position, segment.child_position) > kEpsilon;
            request.centers.push_back(segment.child_position);
            request.radii.push_back(developed ? segment.diameter * 0.5F : 0.0F);
        }
        requests.push_back(std::move(request));
    }
    return requests;
}

[[nodiscard]] std::vector<float> cumulative_lengths(const ChainBuildRequest& request)
{
    std::vector<float> lengths(request.centers.size(), 0.0F);
    for (std::size_t index = 1; index < request.centers.size(); ++index) {
        lengths[index] = lengths[index - 1] + growth::distance(request.centers[index - 1], request.centers[index]);
    }
    return lengths;
}

[[nodiscard]] std::vector<TubeFrame> make_tube_frames(const ChainBuildRequest& request)
{
    std::vector<TubeFrame> frames;
    frames.reserve(request.centers.size());

    growth::Vec3 previous_normal{};
    for (std::size_t ring_index = 0; ring_index < request.centers.size(); ++ring_index) {
        growth::Vec3 tangent{};
        if (ring_index == 0) {
            tangent = growth::subtract(request.centers[1], request.centers[0]);
        } else if (ring_index + 1 == request.centers.size()) {
            tangent = growth::subtract(request.centers[ring_index], request.centers[ring_index - 1]);
        } else {
            tangent = growth::subtract(request.centers[ring_index + 1], request.centers[ring_index - 1]);
        }
        tangent = safe_normalize(tangent, {.x = 0.0F, .y = 0.0F, .z = 1.0F});

        growth::Vec3 normal =
            ring_index == 0 ? fallback_normal_for(tangent) : subtract_projection(previous_normal, tangent);
        normal = safe_normalize(normal, fallback_normal_for(tangent));
        const growth::Vec3 binormal = safe_normalize(cross(tangent, normal), {.x = 0.0F, .y = 1.0F, .z = 0.0F});
        frames.push_back({.tangent = tangent, .normal = normal, .binormal = binormal});
        previous_normal = normal;
    }

    return frames;
}

void add_face(MeshGeometry& mesh, std::initializer_list<std::int32_t> indices, std::initializer_list<Vec2> st,
              std::int32_t source_segment_id)
{
    mesh.face_vertex_counts.push_back(static_cast<std::int32_t>(indices.size()));
    mesh.face_vertex_indices.insert(mesh.face_vertex_indices.end(), indices.begin(), indices.end());
    mesh.st.insert(mesh.st.end(), st.begin(), st.end());
    mesh.face_source_segment_ids.push_back(source_segment_id);
}

void add_ring_points(MeshGeometry& mesh, const ChainBuildRequest& request, const std::vector<TubeFrame>& frames)
{
    for (std::size_t ring_index = 0; ring_index < request.centers.size(); ++ring_index) {
        for (int radial_index = 0; radial_index < kRadialSegmentCount; ++radial_index) {
            mesh.points.push_back(
                growth::add(request.centers[ring_index], growth::scale(ring_direction(frames[ring_index], radial_index),
                                                                       request.radii[ring_index])));
        }
    }
}

void add_side_faces(MeshGeometry& mesh, const ChainBuildRequest& request, const std::vector<float>& lengths)
{
    const float texture_length = std::max(1.0F, lengths.back());
    for (std::size_t segment_index = 0; segment_index < request.source_segment_ids.size(); ++segment_index) {
        const float v0 = lengths[segment_index] / texture_length;
        const float v1 = lengths[segment_index + 1] / texture_length;
        for (int radial_index = 0; radial_index < kRadialSegmentCount; ++radial_index) {
            const int next_radial_index = (radial_index + 1) % kRadialSegmentCount;
            const float u0 = static_cast<float>(radial_index) / static_cast<float>(kRadialSegmentCount);
            const float u1 = static_cast<float>(radial_index + 1) / static_cast<float>(kRadialSegmentCount);

            add_face(mesh,
                     {
                         ring_vertex_index(segment_index, radial_index),
                         ring_vertex_index(segment_index, next_radial_index),
                         ring_vertex_index(segment_index + 1, next_radial_index),
                         ring_vertex_index(segment_index + 1, radial_index),
                     },
                     {
                         {.x = u0, .y = v0},
                         {.x = u1, .y = v0},
                         {.x = u1, .y = v1},
                         {.x = u0, .y = v1},
                     },
                     static_cast<std::int32_t>(request.source_segment_ids[segment_index]));
        }
    }
}

void add_start_cap(MeshGeometry& mesh, const ChainBuildRequest& request)
{
    const auto center_index = static_cast<std::int32_t>(mesh.points.size());
    mesh.points.push_back(request.centers.front());
    for (int radial_index = 0; radial_index < kRadialSegmentCount; ++radial_index) {
        const int next_radial_index = (radial_index + 1) % kRadialSegmentCount;
        add_face(mesh, {center_index, ring_vertex_index(0, next_radial_index), ring_vertex_index(0, radial_index)},
                 {
                     {.x = 0.5F, .y = 0.5F},
                     disk_uv(next_radial_index),
                     disk_uv(radial_index),
                 },
                 static_cast<std::int32_t>(request.source_segment_ids.front()));
    }
}

void add_end_cap(MeshGeometry& mesh, const ChainBuildRequest& request)
{
    const auto center_index = static_cast<std::int32_t>(mesh.points.size());
    mesh.points.push_back(request.centers.back());
    const std::size_t ring_index = request.centers.size() - 1;
    for (int radial_index = 0; radial_index < kRadialSegmentCount; ++radial_index) {
        const int next_radial_index = (radial_index + 1) % kRadialSegmentCount;
        add_face(mesh,
                 {center_index, ring_vertex_index(ring_index, radial_index),
                  ring_vertex_index(ring_index, next_radial_index)},
                 {
                     {.x = 0.5F, .y = 0.5F},
                     disk_uv(radial_index),
                     disk_uv(next_radial_index),
                 },
                 static_cast<std::int32_t>(request.source_segment_ids.back()));
    }
}

[[nodiscard]] MeshGeometry build_mesh(const ChainBuildRequest& request)
{
    MeshGeometry mesh;
    if (request.source_segment_ids.empty() || request.centers.size() < 2 ||
        request.centers.size() != request.radii.size()) {
        return mesh;
    }

    const auto lengths = cumulative_lengths(request);
    const auto frames = make_tube_frames(request);
    add_ring_points(mesh, request, frames);
    add_side_faces(mesh, request, lengths);
    if (request.start_cap) {
        add_start_cap(mesh, request);
    }
    if (request.end_cap) {
        add_end_cap(mesh, request);
    }
    return mesh;
}

std::vector<MeshGeometry> build_meshes(const std::vector<ChainBuildRequest>& requests)
{
    std::vector<MeshGeometry> meshes;
    meshes.reserve(requests.size());
    for (const auto& request : requests) {
        auto mesh = build_mesh(request);
        if (!mesh.points.empty()) {
            meshes.push_back(std::move(mesh));
        }
    }
    return meshes;
}

GrowthPreviewMeshStats stats_for(const std::vector<MeshGeometry>& meshes, std::size_t chain_count)
{
    GrowthPreviewMeshStats stats{
        .chain_count = chain_count,
        .mesh_count = meshes.size(),
    };
    for (const auto& mesh : meshes) {
        stats.vertex_count += mesh.points.size();
        stats.face_count += mesh.face_vertex_counts.size();
    }
    return stats;
}

[[nodiscard]] std::string mesh_prim_path(std::size_t mesh_index)
{
    std::ostringstream path;
    path << "/" << kWorldPrimName << "/" << kGrowthPreviewPrimName << "/" << kChainsPrimName << "/Chain_"
         << std::setw(3) << std::setfill('0') << mesh_index;
    return path.str();
}

std::vector<GrowthPreviewMeshAttributes> mesh_attributes_for(const std::vector<MeshGeometry>& meshes)
{
    std::vector<GrowthPreviewMeshAttributes> attributes;
    attributes.reserve(meshes.size());
    for (std::size_t mesh_index = 0; mesh_index < meshes.size(); ++mesh_index) {
        attributes.push_back({
            .prim_path = mesh_prim_path(mesh_index),
            .points = meshes[mesh_index].points,
        });
    }
    return attributes;
}

void write_mesh(std::ostream& usda, const MeshGeometry& mesh, std::size_t mesh_index)
{
    const auto bounds = mesh_bounds(mesh);
    usda << "            def Mesh \"Chain_" << std::setw(3) << std::setfill('0') << mesh_index << std::setfill(' ')
         << "\"\n";
    usda << "            {\n";
    usda << "                float3[] extent = [";
    write_vec3(usda, bounds.min);
    usda << ", ";
    write_vec3(usda, bounds.max);
    usda << "]\n";
    usda << "                int[] faceVertexCounts = ";
    write_int_list(usda, mesh.face_vertex_counts);
    usda << "\n";
    usda << "                int[] faceVertexIndices = ";
    write_int_list(usda, mesh.face_vertex_indices);
    usda << "\n";
    usda << "                point3f[] points = ";
    write_vec3_list(usda, mesh.points);
    usda << "\n";
    usda << "                texCoord2f[] primvars:st = ";
    write_vec2_list(usda, mesh.st);
    usda << " (\n";
    usda << "                    interpolation = \"faceVarying\"\n";
    usda << "                )\n";
    usda << "                int[] primvars:sourceSegmentId = ";
    write_int_list(usda, mesh.face_source_segment_ids);
    usda << " (\n";
    usda << "                    interpolation = \"uniform\"\n";
    usda << "                )\n";
    usda << "                color3f[] primvars:displayColor = [(0.40, 0.23, 0.12)] (\n";
    usda << "                    interpolation = \"constant\"\n";
    usda << "                )\n";
    usda << "                uniform token subdivisionScheme = \"none\"\n";
    usda << "            }\n";
}

GrowthPreviewUsdStage make_growth_preview_usd_stage(const std::vector<MeshGeometry>& meshes,
                                                    const GrowthPreviewCamera& camera,
                                                    GrowthPreviewStageOptions options)
{
    const int width = camera.width;
    const int height = camera.height;

    std::ostringstream usda;
    usda << std::setprecision(9);
    usda << "#usda 1.0\n";
    usda << "(\n";
    usda << "    defaultPrim = \"" << kWorldPrimName << "\"\n";
    usda << "    metersPerUnit = 1\n";
    usda << "    upAxis = \"Z\"\n";
    usda << ")\n\n";

    usda << "def Xform \"" << kWorldPrimName << "\"\n";
    usda << "{\n";
    usda << "    def Xform \"" << kGrowthPreviewPrimName << "\"\n";
    usda << "    {\n";
    usda << "        def Xform \"" << kChainsPrimName << "\"\n";
    usda << "        {\n";
    for (std::size_t mesh_index = 0; mesh_index < meshes.size(); ++mesh_index) {
        write_mesh(usda, meshes[mesh_index], mesh_index);
    }
    usda << "        }\n";
    usda << "    }\n\n";
    auto hdri_texture_asset_path = options.hdri_texture_path;
    if (!options.asset_search_path.empty() && hdri_texture_asset_path.is_relative()) {
        hdri_texture_asset_path = std::filesystem::absolute(options.asset_search_path / hdri_texture_asset_path);
    }
    hdri_texture_asset_path = hdri_texture_asset_path.lexically_normal();

    usda << "    def DomeLight \"EnvironmentLight\"\n";
    usda << "    {\n";
    usda << "        float inputs:intensity = 1000\n";
    usda << "        asset inputs:texture:file = @" << hdri_texture_asset_path.generic_string() << "@\n";
    usda << "        token inputs:texture:format = \"latlong\"\n";
    if (!options.hdri_visible) {
        usda << "        custom bool visibleInPrimaryRay = 0\n";
    }
    usda << "    }\n";
    usda << "}\n\n";

    usda << "def Camera \"" << prim_name_from_path(kCameraPath) << "\"\n";
    usda << "{\n";
    usda << "    float2 clippingRange = (" << camera.near_clip << ", " << camera.far_clip << ")\n";
    usda << "    float focalLength = " << camera.focal_length << "\n";
    usda << "    float horizontalAperture = " << camera.horizontal_aperture << "\n";
    usda << "    float verticalAperture = " << camera.vertical_aperture << "\n";
    usda << "    token projection = \"perspective\"\n";
    usda << "    matrix4d xformOp:transform = ";
    write_camera_matrix(usda, camera);
    usda << "\n";
    usda << "    uniform token[] xformOpOrder = [\"xformOp:transform\"]\n";
    usda << "}\n\n";

    const std::string render_product_name = prim_name_from_path(kRenderProductPath);
    usda << "def RenderProduct \"" << render_product_name << "\"\n";
    usda << "{\n";
    usda << "    rel camera = <" << kCameraPath << ">\n";
    usda << "    int2 resolution = (" << width << ", " << height << ")\n";
    usda << "    uint[] deviceIds = [0]\n";
    usda << "    token productType = \"raster\"\n";
    usda << "    rel orderedVars = [<" << kRenderProductPath << "/LdrColor>, <" << kRenderProductPath
         << "/DistanceToCameraSD>]\n\n";
    usda << "    def RenderVar \"LdrColor\"\n";
    usda << "    {\n";
    usda << "        uniform string sourceName = \"LdrColor\"\n";
    usda << "    }\n\n";
    usda << "    def RenderVar \"DistanceToCameraSD\"\n";
    usda << "    {\n";
    usda << "        uniform string sourceName = \"DistanceToCameraSD\"\n";
    usda << "    }\n";
    usda << "}\n\n";

    usda << "def RenderSettings \"" << prim_name_from_path(kRenderSettingsPath) << "\"\n";
    usda << "{\n";
    usda << "    rel products = [<" << kRenderProductPath << ">]\n";
    usda << "}\n";

    return {
        .text = usda.str(),
        .render_product_path = std::string(kRenderProductPath),
        .camera_path = std::string(kCameraPath),
        .asset_search_path = std::move(options.asset_search_path),
        .hdri_texture_path = std::move(options.hdri_texture_path),
        .width = width,
        .height = height,
    };
}

} // namespace
} // namespace toi::render
