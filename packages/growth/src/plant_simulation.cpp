#include "toi/growth/growth.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <numbers>
#include <queue>
#include <utility>

namespace toi::growth {
namespace {

constexpr float kCentimetersToMeters = 0.01F;
constexpr float kOrientationStep = 10.0F * std::numbers::pi_v<float> / 180.0F;
constexpr int kOrientationIterations = 3;

struct ModuleGeometry {
    std::vector<std::optional<GrowthSnapshotSegment>> current_segments;
    std::vector<GrowthSnapshotSegment> mature_segments;
    std::vector<Vec3> current_nodes;
    std::vector<Vec3> mature_nodes;
    Sphere sphere;
};

struct OrientedChild {
    RigidTransform transform;
    Sphere mature_sphere;
};

struct EulerAngles {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
};

[[nodiscard]] GrowthError invalid_input(std::string message);
[[nodiscard]] GrowthError invalid_prototype(std::string message);
[[nodiscard]] float dot(Vec3 left, Vec3 right);
[[nodiscard]] Vec3 cross(Vec3 left, Vec3 right);
[[nodiscard]] Vec3 rotate_about_axis(Vec3 value, Vec3 axis, float angle);
[[nodiscard]] RigidTransform rotated_transform(const RigidTransform& transform, Vec3 axis, float angle);
[[nodiscard]] EulerAngles euler_angles(const RigidTransform& transform);
[[nodiscard]] RigidTransform transform_for_euler(EulerAngles angles, Vec3 translation);
[[nodiscard]] Sphere sphere_for_points(std::span<const Vec3> points);
[[nodiscard]] float segment_diameter_maturity(const BranchModulePrototype& prototype,
                                              const BranchSegment& segment,
                                              float module_physiological_age);
[[nodiscard]] Result<ModuleGeometry> build_geometry(const BranchModulePrototype& prototype,
                                                    const PlantTypeParameters& plant_type,
                                                    const RigidTransform& transform,
                                                    float physiological_age);
[[nodiscard]] OrientedChild orient_child(const RigidTransform& inherited, Vec3 translation,
                                         Vec3 terminal_tangent,
                                         const BranchModulePrototype& child_prototype, Sphere mature_local_sphere,
                                         const PlantTypeParameters& plant_type,
                                         std::span<const Sphere> existing_spheres);

} // namespace

struct PlantSimulation::ConduitWorkset {
    struct Edge {
        std::size_t module_index = 0;
        std::size_t source_segment_id = 0;
        std::size_t parent_node = 0;
        std::size_t child_node = 0;
    };

    std::vector<std::size_t> module_node_offsets;
    std::vector<std::size_t> module_edge_offsets;
    std::vector<std::size_t> global_node_by_local_node;
    std::vector<std::optional<std::size_t>> child_module_by_local_node;
    std::vector<Edge> edges;
    std::vector<std::size_t> child_edge_offsets;
    std::vector<std::size_t> child_edges;
    std::vector<std::size_t> preorder_edges;
    std::vector<std::size_t> postorder_edges;
    std::vector<float> current_diameters;
};

Vec3 transform_direction(const RigidTransform& transform, Vec3 direction)
{
    return add(add(scale(transform.x_axis, direction.x), scale(transform.y_axis, direction.y)),
               scale(transform.z_axis, direction.z));
}

Vec3 transform_point(const RigidTransform& transform, Vec3 point)
{
    return add(transform.translation, transform_direction(transform, point));
}

Result<PlantSimulation> PlantSimulation::create(const BranchModulePrototypeLibrary& prototype_library,
                                                const PlantTypeParameters& plant_type,
                                                std::size_t root_prototype_id)
{
    if (!plant_type_parameters_are_valid(plant_type)) {
        return std::unexpected(invalid_input("plant type parameters are invalid"));
    }

    PlantSimulation simulation;
    simulation.plant_type_ = plant_type;
    simulation.prepared_prototypes_.reserve(prototype_library.prototypes.size());
    simulation.prototype_orientation_data_.reserve(prototype_library.prototypes.size());
    for (const auto& prototype : prototype_library.prototypes) {
        if (std::ranges::any_of(simulation.prepared_prototypes_, [&](const auto& prepared) {
                return prepared.id == prototype.id;
            })) {
            return std::unexpected(invalid_prototype("prototype ids must be unique"));
        }
        auto prepared = prepare_branch_module_prototype(prototype, plant_type);
        if (!prepared) {
            return std::unexpected(prepared.error());
        }
        if (prepared->child_segments_by_node[prepared->root_node].size() > 1) {
            return std::unexpected(invalid_prototype("prototype root forks require an allocation policy"));
        }
        auto mature_geometry = build_geometry(*prepared, plant_type, {}, prepared->max_physiological_age);
        if (!mature_geometry) {
            return std::unexpected(mature_geometry.error());
        }
        const auto terminal_tangent = [&](std::size_t terminal_node) {
            const auto incoming_segment = *prepared->incoming_segment_by_node[terminal_node];
            const auto& segment = mature_geometry->mature_segments[incoming_segment];
            return normalize(subtract(segment.child_position, segment.parent_position));
        };
        const Vec3 main_tangent = terminal_tangent(prepared->main_axis_terminal_node);
        auto ordered_terminal_nodes = prepared->terminal_nodes;
        std::ranges::sort(ordered_terminal_nodes, [&](std::size_t left, std::size_t right) {
            if (left == right) {
                return false;
            }
            if (left == prepared->main_axis_terminal_node) {
                return true;
            }
            if (right == prepared->main_axis_terminal_node) {
                return false;
            }
            const float left_alignment = dot(main_tangent, terminal_tangent(left));
            const float right_alignment = dot(main_tangent, terminal_tangent(right));
            return left_alignment != right_alignment ? left_alignment > right_alignment : left < right;
        });
        simulation.prototype_orientation_data_.push_back({
            .mature_sphere = mature_geometry->sphere,
            .ordered_terminal_nodes = std::move(ordered_terminal_nodes),
        });
        simulation.prepared_prototypes_.push_back(std::move(*prepared));
    }
    for (std::size_t id = 0; id < 9; ++id) {
        if (!std::ranges::any_of(simulation.prepared_prototypes_,
                                 [id](const auto& prototype) { return prototype.id == id; })) {
            return std::unexpected(invalid_prototype("morphospace requires prototype ids 0 through 8"));
        }
    }

    const auto root = std::ranges::find_if(simulation.prepared_prototypes_,
                                           [root_prototype_id](const auto& prototype) {
                                               return prototype.id == root_prototype_id;
                                           });
    if (root == simulation.prepared_prototypes_.end()) {
        return std::unexpected(invalid_prototype("root prototype does not exist"));
    }
    const std::size_t root_index = static_cast<std::size_t>(root - simulation.prepared_prototypes_.begin());
    auto mature_age = fully_grown_age(*root, plant_type);
    if (!mature_age) {
        return std::unexpected(mature_age.error());
    }
    simulation.module_records_.push_back({
        .id = 0,
        .prototype_index = root_index,
        .transform = {},
        .physiological_age = 0.0F,
        .fully_grown_age = *mature_age,
        .developed_diameters = std::vector<float>(
            root->segments.size(), plant_type.terminal_thickness * kCentimetersToMeters),
        .diagnostics_active = true,
    });
    simulation.snapshot_attachment_events_.clear();
    simulation.snapshot_flow_diagnostics_.clear();
    auto rebuilt = simulation.rebuild_snapshot(false);
    if (!rebuilt) {
        return std::unexpected(rebuilt.error());
    }
    return simulation;
}

Result<void> PlantSimulation::step(float timestep)
{
    if (!std::isfinite(timestep) || timestep <= 0.0F) {
        return std::unexpected(invalid_input("plant timestep must be finite and positive"));
    }
    if (!std::isfinite(plant_age_ + timestep)) {
        return std::unexpected(invalid_input("plant age must remain finite"));
    }

    try {
        PlantSimulation next = *this;
        next.snapshot_attachment_events_.clear();
        for (auto& module : next.module_records_) {
            module.diagnostics_active = true;
        }
        auto current = next.rebuild_snapshot(false);
        if (!current) {
            return std::unexpected(current.error());
        }
        const auto precommit_root = std::ranges::find(next.snapshot_modules_, 0, &PlantModuleSnapshot::id);
        const float precommit_root_vigor = precommit_root->vigor;
        std::vector<Sphere> orientation_spheres;
        orientation_spheres.reserve(next.snapshot_modules_.size() + next.snapshot_terminals_.size());
        for (const auto& module : next.snapshot_modules_) {
            orientation_spheres.push_back(module.collision_sphere);
        }

        std::vector<float> old_ages;
        old_ages.reserve(next.module_records_.size());
        for (auto& module : next.module_records_) {
            old_ages.push_back(module.physiological_age);
            const auto snapshot_module = std::ranges::find(next.snapshot_modules_, module.id,
                                                           &PlantModuleSnapshot::id);
            module.physiological_age = std::min(
                module.fully_grown_age,
                physiological_age_euler_step(module.physiological_age, snapshot_module->growth_rate, timestep));
        }

        const auto root_record = std::ranges::find(next.module_records_, 0, &ModuleRecord::id);
        const std::size_t root_record_index = static_cast<std::size_t>(root_record - next.module_records_.begin());
        const RigidTransform root_transform = root_record->transform;
        const bool root_crossed = old_ages[root_record_index] < root_record->fully_grown_age &&
                                  root_record->physiological_age >= root_record->fully_grown_age;
        if (root_crossed) {
            auto crossing_state = next.rebuild_snapshot(false);
            if (!crossing_state) {
                return std::unexpected(crossing_state.error());
            }
            const float determinacy = parameter_for_plant_age(
                next.plant_type_.determinacy, next.plant_type_.mature_determinacy,
                next.plant_type_.flowering_age, next.plant_age_);
            const float apical_control = parameter_for_plant_age(
                next.plant_type_.apical_control, next.plant_type_.mature_apical_control,
                next.plant_type_.flowering_age, next.plant_age_);
            const std::size_t selected_id = nearest_morphospace_prototype(
                apical_control, vigor_scaled_determinacy(determinacy, precommit_root_vigor));
            const auto selected = std::ranges::find(next.prepared_prototypes_, selected_id,
                                                    &BranchModulePrototype::id);
            const std::size_t selected_index =
                static_cast<std::size_t>(selected - next.prepared_prototypes_.begin());
            auto selected_mature_age = fully_grown_age(*selected, next.plant_type_);
            if (!selected_mature_age) {
                return std::unexpected(selected_mature_age.error());
            }

            const auto& ordered_terminal_nodes =
                next.prototype_orientation_data_[root_record->prototype_index].ordered_terminal_nodes;
            for (const auto terminal_node : ordered_terminal_nodes) {
                const auto terminal = std::ranges::find_if(next.snapshot_terminals_, [&](const auto& value) {
                    return value.module_id == 0 && value.terminal_node == terminal_node;
                });
                if (terminal == next.snapshot_terminals_.end() || terminal->child_module_id ||
                    terminal->vigor <= kMinimumModuleVigor) {
                    continue;
                }
                const std::size_t child_id = next.next_module_id_++;
                const OrientedChild oriented = orient_child(
                    root_transform, terminal->position, terminal->tangent, *selected,
                    next.prototype_orientation_data_[selected_index].mature_sphere,
                    next.plant_type_, orientation_spheres);
                next.module_records_.push_back({
                    .id = child_id,
                    .prototype_index = selected_index,
                    .parent_module_index = root_record_index,
                    .parent_terminal_node = terminal_node,
                    .transform = oriented.transform,
                    .physiological_age = 0.0F,
                    .fully_grown_age = *selected_mature_age,
                    .developed_diameters = std::vector<float>(
                        selected->segments.size(), next.plant_type_.terminal_thickness * kCentimetersToMeters),
                    .diagnostics_active = false,
                });
                next.snapshot_attachment_events_.push_back({
                    .child_module_id = child_id,
                    .parent_module_id = 0,
                    .parent_terminal_node = terminal_node,
                    .prototype_id = selected_id,
                });
                orientation_spheres.push_back(oriented.mature_sphere);
            }
        }

        next.plant_age_ += timestep;
        auto committed = next.rebuild_snapshot(true);
        if (!committed) {
            return std::unexpected(committed.error());
        }
        *this = std::move(next);
        return {};
    } catch (const std::bad_alloc&) {
        return std::unexpected(GrowthError{
            .code = GrowthError::Code::ResourceExhausted,
            .message = "plant step exhausted memory",
        });
    }
}

Result<void> PlantSimulation::set_flow_diagnostics(PlantFlowDiagnosticOptions options)
{
    const auto previous_options = flow_diagnostic_options_;
    try {
        if (!options.accumulated_light && !options.vigor) {
            flow_diagnostic_options_ = options;
            std::vector<PlantFlowDiagnostic>().swap(snapshot_flow_diagnostics_);
            return {};
        }

        ConduitWorkset conduit;
        if (auto rebuilt = rebuild_conduit(conduit); !rebuilt) return rebuilt;
        flow_diagnostic_options_ = options;
        if (auto rebuilt = rebuild_flow_diagnostics(conduit); !rebuilt) {
            flow_diagnostic_options_ = previous_options;
            return rebuilt;
        }
        return {};
    } catch (const std::bad_alloc&) {
        flow_diagnostic_options_ = previous_options;
        return std::unexpected(GrowthError{
            .code = GrowthError::Code::ResourceExhausted,
            .message = "plant flow diagnostics exhausted memory",
        });
    }
}

PlantSnapshot PlantSimulation::snapshot() const
{
    return {
        .plant_age = plant_age_,
        .modules = snapshot_modules_,
        .segments = snapshot_segments_,
        .mature_terminals = snapshot_terminals_,
        .flow_diagnostics = snapshot_flow_diagnostics_,
        .attachment_events = snapshot_attachment_events_,
    };
}

Result<void> PlantSimulation::rebuild_conduit(ConduitWorkset& workset)
{
    const std::size_t module_count = module_records_.size();
    workset = {};
    workset.module_node_offsets.assign(module_count + 1, 0);
    workset.module_edge_offsets.assign(module_count + 1, 0);
    for (std::size_t module = 0; module < module_count; ++module) {
        const auto& prototype = prepared_prototypes_[module_records_[module].prototype_index];
        if (module_records_[module].developed_diameters.size() != prototype.segments.size()) {
            return std::unexpected(invalid_prototype("module pipe state does not match its prototype"));
        }
        workset.module_node_offsets[module + 1] = workset.module_node_offsets[module] + prototype.nodes.size();
        workset.module_edge_offsets[module + 1] = workset.module_edge_offsets[module] + prototype.segments.size();
    }

    const auto unassigned = std::numeric_limits<std::size_t>::max();
    workset.global_node_by_local_node.assign(workset.module_node_offsets.back(), unassigned);
    workset.child_module_by_local_node.resize(workset.module_node_offsets.back());
    std::size_t node_count = 0;
    for (std::size_t module = 0; module < module_count; ++module) {
        const auto& record = module_records_[module];
        const auto& prototype = prepared_prototypes_[record.prototype_index];
        if (record.parent_module_index) {
            if (*record.parent_module_index >= module || !record.parent_terminal_node) {
                return std::unexpected(invalid_prototype("module attachments must follow parent-first order"));
            }
            const auto parent_local_node = workset.module_node_offsets[*record.parent_module_index] +
                *record.parent_terminal_node;
            workset.global_node_by_local_node[workset.module_node_offsets[module] + prototype.root_node] =
                workset.global_node_by_local_node[parent_local_node];
            workset.child_module_by_local_node[parent_local_node] = module;
        }
        for (std::size_t node = 0; node < prototype.nodes.size(); ++node) {
            auto& global_node = workset.global_node_by_local_node[workset.module_node_offsets[module] + node];
            if (global_node == unassigned) global_node = node_count++;
        }
    }

    workset.edges.reserve(workset.module_edge_offsets.back());
    std::vector<std::size_t> child_edge_counts(node_count, 0);
    for (std::size_t module = 0; module < module_count; ++module) {
        const auto& prototype = prepared_prototypes_[module_records_[module].prototype_index];
        for (std::size_t source = 0; source < prototype.segments.size(); ++source) {
            const auto& segment = prototype.segments[source];
            const auto parent_node = workset.global_node_by_local_node[
                workset.module_node_offsets[module] + segment.parent_node];
            const auto child_node = workset.global_node_by_local_node[
                workset.module_node_offsets[module] + segment.child_node];
            workset.edges.push_back({
                .module_index = module,
                .source_segment_id = source,
                .parent_node = parent_node,
                .child_node = child_node,
            });
            ++child_edge_counts[parent_node];
        }
    }

    workset.child_edge_offsets.assign(node_count + 1, 0);
    for (std::size_t node = 0; node < node_count; ++node) {
        workset.child_edge_offsets[node + 1] = workset.child_edge_offsets[node] + child_edge_counts[node];
    }
    workset.child_edges.resize(workset.edges.size());
    auto child_edge_cursors = workset.child_edge_offsets;
    for (std::size_t edge = 0; edge < workset.edges.size(); ++edge) {
        workset.child_edges[child_edge_cursors[workset.edges[edge].parent_node]++] = edge;
    }

    workset.preorder_edges.reserve(workset.edges.size());
    std::queue<std::size_t> nodes;
    if (!module_records_.empty()) {
        const auto& root_prototype = prepared_prototypes_[module_records_.front().prototype_index];
        nodes.push(workset.global_node_by_local_node[root_prototype.root_node]);
    }
    while (!nodes.empty()) {
        const auto node = nodes.front();
        nodes.pop();
        for (std::size_t index = workset.child_edge_offsets[node];
             index < workset.child_edge_offsets[node + 1]; ++index) {
            const auto edge = workset.child_edges[index];
            workset.preorder_edges.push_back(edge);
            nodes.push(workset.edges[edge].child_node);
        }
    }
    if (workset.preorder_edges.size() != workset.edges.size()) {
        return std::unexpected(invalid_prototype("plant conduit must be one rooted tree"));
    }
    workset.postorder_edges.assign(workset.preorder_edges.rbegin(), workset.preorder_edges.rend());

    const float terminal_thickness = plant_type_.terminal_thickness * kCentimetersToMeters;
    workset.current_diameters.assign(workset.edges.size(), terminal_thickness);
    for (const auto edge_index : workset.postorder_edges) {
        const auto& edge = workset.edges[edge_index];
        const auto& record = module_records_[edge.module_index];
        const auto& prototype = prepared_prototypes_[record.prototype_index];
        const auto& segment = prototype.segments[edge.source_segment_id];
        float child_area = 0.0F;
        for (std::size_t index = workset.child_edge_offsets[edge.child_node];
             index < workset.child_edge_offsets[edge.child_node + 1]; ++index) {
            const float child_diameter = workset.current_diameters[workset.child_edges[index]];
            child_area += child_diameter * child_diameter;
        }
        // Paper: d_b, supported segment diameter; φ, terminal_thickness.
        const float support_diameter = child_area > 0.0F ? std::sqrt(child_area) : terminal_thickness;
        const float maturity = segment_diameter_maturity(prototype, segment, record.physiological_age);
        const float candidate = terminal_thickness + (support_diameter - terminal_thickness) * maturity;
        auto& developed = module_records_[edge.module_index].developed_diameters[edge.source_segment_id];
        developed = std::max(developed, candidate);
        workset.current_diameters[edge_index] = developed;
    }
    return {};
}

Result<void> PlantSimulation::rebuild_flow_diagnostics(const ConduitWorkset& workset)
{
    if (!flow_diagnostic_options_.accumulated_light && !flow_diagnostic_options_.vigor) {
        std::vector<PlantFlowDiagnostic>().swap(snapshot_flow_diagnostics_);
        return {};
    }
    if (snapshot_segments_.size() != workset.edges.size() || snapshot_modules_.size() != module_records_.size()) {
        return std::unexpected(invalid_prototype("plant conduit does not match its snapshot"));
    }

    const std::size_t node_count = workset.child_edge_offsets.size() - 1;
    std::vector<bool> active_edges(workset.edges.size(), false);
    for (std::size_t edge = 0; edge < workset.edges.size(); ++edge) {
        active_edges[edge] = distance(snapshot_segments_[edge].parent_position,
                                      snapshot_segments_[edge].child_position) > kEpsilon;
    }

    std::vector<float> node_light(node_count, 0.0F);
    std::queue<std::size_t> local_nodes;
    std::vector<std::size_t> frontier_nodes;
    for (std::size_t module = 0; module < module_records_.size(); ++module) {
        const auto& prototype = prepared_prototypes_[module_records_[module].prototype_index];
        while (!local_nodes.empty()) local_nodes.pop();
        frontier_nodes.clear();
        local_nodes.push(prototype.root_node);
        while (!local_nodes.empty()) {
            const auto local_node = local_nodes.front();
            local_nodes.pop();
            bool has_active_child = false;
            for (const auto source : prototype.child_segments_by_node[local_node]) {
                const auto edge = workset.module_edge_offsets[module] + source;
                if (!active_edges[edge]) continue;
                has_active_child = true;
                local_nodes.push(prototype.segments[source].child_node);
            }
            if (!has_active_child) {
                frontier_nodes.push_back(workset.global_node_by_local_node[
                    workset.module_node_offsets[module] + local_node]);
            }
        }
        const float share = snapshot_modules_[module].direct_light_exposure /
            static_cast<float>(frontier_nodes.size());
        for (const auto node : frontier_nodes) node_light[node] += share;
    }

    std::vector<float> edge_light(workset.edges.size(), 0.0F);
    for (const auto edge : workset.postorder_edges) {
        if (!active_edges[edge]) continue;
        const auto& conduit_edge = workset.edges[edge];
        edge_light[edge] = node_light[conduit_edge.child_node];
        node_light[conduit_edge.parent_node] += edge_light[edge];
    }

    const auto& root_prototype = prepared_prototypes_[module_records_.front().prototype_index];
    const auto root_node = workset.global_node_by_local_node[root_prototype.root_node];
    const float root_light_total = node_light[root_node];
    const float root_vigor_total = std::min(root_light_total, plant_type_.root_max_vigor);
    std::vector<float> edge_vigor;
    if (flow_diagnostic_options_.vigor) {
        edge_vigor.assign(workset.edges.size(), 0.0F);
        std::vector<std::optional<std::size_t>> main_child_edge_by_node(node_count);
        for (std::size_t module = 0; module < module_records_.size(); ++module) {
            const auto& prototype = prepared_prototypes_[module_records_[module].prototype_index];
            for (std::size_t node = 0; node < prototype.nodes.size(); ++node) {
                if (const auto main = prototype.main_child_segment_by_node[node]) {
                    const auto global_node = workset.global_node_by_local_node[
                        workset.module_node_offsets[module] + node];
                    main_child_edge_by_node[global_node] = workset.module_edge_offsets[module] + *main;
                }
            }
        }
        std::vector<float> node_vigor(node_count, 0.0F);
        node_vigor[root_node] = root_vigor_total;
        std::queue<std::size_t> nodes;
        nodes.push(root_node);
        const float apical_control = parameter_for_plant_age(
            plant_type_.apical_control, plant_type_.mature_apical_control,
            plant_type_.flowering_age, plant_age_);
        while (!nodes.empty()) {
            const auto node = nodes.front();
            nodes.pop();
            std::size_t active_child_count = 0;
            std::size_t first_active_child = 0;
            for (std::size_t index = workset.child_edge_offsets[node];
                 index < workset.child_edge_offsets[node + 1]; ++index) {
                const auto edge = workset.child_edges[index];
                if (!active_edges[edge]) continue;
                if (active_child_count == 0) first_active_child = edge;
                ++active_child_count;
            }
            if (active_child_count == 0) continue;
            float available_vigor = node_vigor[node];
            const auto& first_edge = workset.edges[first_active_child];
            const auto& first_prototype =
                prepared_prototypes_[module_records_[first_edge.module_index].prototype_index];
            if (first_edge.module_index != 0 &&
                first_prototype.segments[first_edge.source_segment_id].parent_node == first_prototype.root_node) {
                available_vigor = snapshot_modules_[first_edge.module_index].vigor;
            }
            if (active_child_count == 1) {
                edge_vigor[first_active_child] = available_vigor;
                node_vigor[workset.edges[first_active_child].child_node] = edge_vigor[first_active_child];
                nodes.push(workset.edges[first_active_child].child_node);
                continue;
            }

            auto main_edge = first_active_child;
            if (const auto main = main_child_edge_by_node[node];
                *main < active_edges.size() && active_edges[*main]) {
                main_edge = *main;
            }
            float lateral_light = 0.0F;
            std::size_t lateral_count = 0;
            for (std::size_t index = workset.child_edge_offsets[node];
                 index < workset.child_edge_offsets[node + 1]; ++index) {
                const auto edge = workset.child_edges[index];
                if (!active_edges[edge] || edge == main_edge) continue;
                lateral_light += edge_light[edge];
                ++lateral_count;
            }
            const auto split = split_vigor(available_vigor, edge_light[main_edge],
                                           lateral_light, apical_control);
            edge_vigor[main_edge] = split.main_axis;
            node_vigor[workset.edges[main_edge].child_node] = edge_vigor[main_edge];
            nodes.push(workset.edges[main_edge].child_node);
            for (std::size_t index = workset.child_edge_offsets[node];
                 index < workset.child_edge_offsets[node + 1]; ++index) {
                const auto edge = workset.child_edges[index];
                if (!active_edges[edge] || edge == main_edge) continue;
                edge_vigor[edge] = lateral_light > kEpsilon
                    ? split.lateral_axis * edge_light[edge] / lateral_light
                    : split.lateral_axis / static_cast<float>(lateral_count);
                node_vigor[workset.edges[edge].child_node] = edge_vigor[edge];
                nodes.push(workset.edges[edge].child_node);
            }
        }
    }

    std::vector<float> node_distance_from_root(node_count, 0.0F);
    for (const auto edge : workset.preorder_edges) {
        const auto& conduit_edge = workset.edges[edge];
        node_distance_from_root[conduit_edge.child_node] =
            node_distance_from_root[conduit_edge.parent_node] +
            (active_edges[edge]
                 ? distance(snapshot_segments_[edge].parent_position, snapshot_segments_[edge].child_position)
                 : 0.0F);
    }

    std::vector<PlantFlowDiagnostic> next;
    next.reserve(workset.edges.size() *
                 static_cast<std::size_t>(flow_diagnostic_options_.accumulated_light +
                                          flow_diagnostic_options_.vigor));
    for (std::size_t edge = 0; edge < workset.edges.size(); ++edge) {
        if (!active_edges[edge]) continue;
        const auto& conduit_edge = workset.edges[edge];
        const float start_distance = node_distance_from_root[conduit_edge.parent_node];
        const float end_distance = node_distance_from_root[conduit_edge.child_node];
        if (flow_diagnostic_options_.accumulated_light && edge_light[edge] > kEpsilon &&
            root_light_total > kEpsilon) {
            next.push_back({
                .kind = FlowKind::AccumulatedLight,
                .segment_index = edge,
                .amount = edge_light[edge],
                .root_total = root_light_total,
                .start_distance_from_root = start_distance,
                .end_distance_from_root = end_distance,
            });
        }
        if (flow_diagnostic_options_.vigor && edge_vigor[edge] > kEpsilon && root_vigor_total > kEpsilon) {
            next.push_back({
                .kind = FlowKind::Vigor,
                .segment_index = edge,
                .amount = edge_vigor[edge],
                .root_total = root_vigor_total,
                .start_distance_from_root = start_distance,
                .end_distance_from_root = end_distance,
            });
        }
    }
    snapshot_flow_diagnostics_ = std::move(next);
    return {};
}

Result<void> PlantSimulation::rebuild_snapshot(bool include_flow_diagnostics)
{
    const std::size_t module_count = module_records_.size();
    ConduitWorkset conduit;
    if (auto rebuilt = rebuild_conduit(conduit); !rebuilt) return rebuilt;
    const auto& module_node_offsets = conduit.module_node_offsets;
    const auto& child_module_by_node = conduit.child_module_by_local_node;

    std::vector<ModuleGeometry> geometries;
    geometries.reserve(module_count);
    for (const auto& module : module_records_) {
        auto geometry = build_geometry(prepared_prototypes_[module.prototype_index], plant_type_,
                                       module.transform, module.physiological_age);
        if (!geometry) {
            return std::unexpected(geometry.error());
        }
        geometries.push_back(std::move(*geometry));
    }

    std::vector<float> direct_light(module_count, 0.0F);
    for (std::size_t module = 0; module < module_count; ++module) {
        if (!module_records_[module].diagnostics_active) {
            continue;
        }
        std::vector<Sphere> others;
        others.reserve(module_count - 1);
        for (std::size_t other = 0; other < module_count; ++other) {
            if (other != module && module_records_[other].diagnostics_active) {
                others.push_back(geometries[other].sphere);
            }
        }
        direct_light[module] = light_exposure(collision_measure(geometries[module].sphere, others));
    }

    std::vector<float> accumulated(module_count, 0.0F);
    std::vector<std::vector<float>> node_light(module_count);
    for (std::size_t reverse = module_count; reverse > 0; --reverse) {
        const std::size_t module = reverse - 1;
        const auto& record = module_records_[module];
        const auto& prototype = prepared_prototypes_[record.prototype_index];
        node_light[module].assign(prototype.nodes.size(), 0.0F);
        if (!record.diagnostics_active) {
            continue;
        }
        if (record.physiological_age + kEpsilon < record.fully_grown_age) {
            accumulated[module] = direct_light[module];
            node_light[module][prototype.root_node] = direct_light[module];
            continue;
        }
        const float terminal_share = direct_light[module] / static_cast<float>(prototype.terminal_nodes.size());
        for (const auto terminal : prototype.terminal_nodes) {
            float terminal_light = terminal_share;
            if (const auto child = child_module_by_node[module_node_offsets[module] + terminal]) {
                terminal_light += accumulated[*child];
            }
            node_light[module][terminal] = terminal_light;
        }
        const std::function<float(std::size_t)> accumulate_from_node = [&](std::size_t node) {
            float total = node_light[module][node];
            for (const auto child_segment : prototype.child_segments_by_node[node]) {
                total += accumulate_from_node(prototype.segments[child_segment].child_node);
            }
            node_light[module][node] = total;
            return total;
        };
        accumulated[module] = accumulate_from_node(prototype.root_node);
    }

    std::vector<float> module_vigor(module_count, 0.0F);
    std::vector<float> module_growth(module_count, 0.0F);
    std::vector<std::vector<float>> node_vigor(module_count);
    const float root_vigor_budget = module_records_.empty()
        ? 0.0F
        : std::min(accumulated[0], plant_type_.root_max_vigor);
    if (!module_records_.empty()) {
        module_vigor[0] = std::min(root_vigor_budget, kMaximumModuleVigor);
    }
    const float apical_control = parameter_for_plant_age(
        plant_type_.apical_control, plant_type_.mature_apical_control,
        plant_type_.flowering_age, plant_age_);
    for (std::size_t module = 0; module < module_count; ++module) {
        const auto& record = module_records_[module];
        const auto& prototype = prepared_prototypes_[record.prototype_index];
        node_vigor[module].assign(prototype.nodes.size(), 0.0F);
        if (!record.diagnostics_active) {
            continue;
        }
        module_vigor[module] = std::min(module_vigor[module], kMaximumModuleVigor);
        auto rate = growth_rate(plant_type_, {
            .vigor = module_vigor[module],
            .min_vigor = kMinimumModuleVigor,
            .max_vigor = kMaximumModuleVigor,
        });
        if (!rate) {
            return std::unexpected(rate.error());
        }
        module_growth[module] = *rate;
        node_vigor[module][prototype.root_node] = module == 0 ? root_vigor_budget : module_vigor[module];
        std::queue<std::size_t> queue;
        queue.push(prototype.root_node);
        while (!queue.empty()) {
            const auto node = queue.front();
            queue.pop();
            const auto& children = prototype.child_segments_by_node[node];
            if (children.empty()) {
                continue;
            }
            if (children.size() == 1) {
                node_vigor[module][prototype.segments[children.front()].child_node] = node_vigor[module][node];
            } else {
                const auto main_segment = prototype.main_child_segment_by_node[node];
                if (!main_segment) {
                    return std::unexpected(invalid_prototype("fork has no precomputed main continuation"));
                }
                const auto main_node = prototype.segments[*main_segment].child_node;
                float lateral_light = 0.0F;
                std::size_t lateral_count = 0;
                for (const auto child_segment : children) {
                    if (child_segment != *main_segment) {
                        lateral_light += node_light[module][prototype.segments[child_segment].child_node];
                        ++lateral_count;
                    }
                }
                const auto split = split_vigor(node_vigor[module][node], node_light[module][main_node],
                                               lateral_light, apical_control);
                node_vigor[module][main_node] = split.main_axis;
                for (const auto child_segment : children) {
                    if (child_segment == *main_segment) continue;
                    const auto child_node = prototype.segments[child_segment].child_node;
                    node_vigor[module][child_node] = lateral_light > kEpsilon
                        ? split.lateral_axis * node_light[module][child_node] / lateral_light
                        : split.lateral_axis / static_cast<float>(lateral_count);
                }
            }
            for (const auto child_segment : children) {
                queue.push(prototype.segments[child_segment].child_node);
            }
        }
        if (record.physiological_age + kEpsilon >= record.fully_grown_age) {
            for (const auto terminal : prototype.terminal_nodes) {
                if (const auto child = child_module_by_node[module_node_offsets[module] + terminal]) {
                    if (module_records_[*child].diagnostics_active) {
                        module_vigor[*child] = std::min(node_vigor[module][terminal], kMaximumModuleVigor);
                    }
                }
            }
        }
    }

    std::vector<PlantModuleSnapshot> next_modules;
    std::vector<PlantSegmentSnapshot> next_segments;
    std::vector<MatureTerminalSnapshot> next_terminals;
    next_modules.reserve(module_count);

    for (std::size_t module = 0; module < module_count; ++module) {
        const auto& record = module_records_[module];
        const auto& prototype = prepared_prototypes_[record.prototype_index];
        const auto range_offset = next_segments.size();
        for (std::size_t source = 0; source < prototype.segments.size(); ++source) {
            const auto& definition = prototype.segments[source];
            const auto& current = geometries[module].current_segments[source];
            const auto conduit_edge = conduit.module_edge_offsets[module] + source;
            const float diameter = conduit.current_diameters[conduit_edge];
            std::optional<std::size_t> continuation;
            if (const auto main = prototype.main_child_segment_by_node[definition.child_node]) {
                continuation = conduit.module_edge_offsets[module] + *main;
            } else if (const auto child = child_module_by_node[
                           module_node_offsets[module] + definition.child_node]) {
                const auto& child_prototype =
                    prepared_prototypes_[module_records_[*child].prototype_index];
                continuation = conduit.module_edge_offsets[*child] +
                    child_prototype.child_segments_by_node[child_prototype.root_node].front();
            }
            next_segments.push_back({
                .module_id = record.id,
                .source_segment_id = source,
                .parent_position = current ? current->parent_position : geometries[module].current_nodes[definition.parent_node],
                .child_position = current ? current->child_position : geometries[module].current_nodes[definition.child_node],
                .mature_parent_position = geometries[module].mature_segments[source].parent_position,
                .mature_child_position = geometries[module].mature_segments[source].child_position,
                .diameter = diameter,
                .state = current ? current->state : SegmentState::Growing,
                .main_continuation_segment = continuation,
            });
        }

        if (record.physiological_age + kEpsilon >= record.fully_grown_age) {
            for (const auto terminal : prototype.terminal_nodes) {
                const auto incoming = prototype.incoming_segment_by_node[terminal];
                const auto& segment = next_segments[range_offset + *incoming];
                const auto child = child_module_by_node[module_node_offsets[module] + terminal];
                next_terminals.push_back({
                    .module_id = record.id,
                    .terminal_node = terminal,
                    .position = geometries[module].mature_nodes[terminal],
                    .tangent = normalize(subtract(segment.mature_child_position, segment.mature_parent_position)),
                    .host_radius = segment.diameter * 0.5F,
                    .vigor = node_vigor[module][terminal],
                    .axis_role = terminal == prototype.main_axis_terminal_node
                        ? TerminalAxisRole::Main
                        : TerminalAxisRole::Lateral,
                    .child_module_id = child
                        ? std::optional<std::size_t>{module_records_[*child].id}
                        : std::nullopt,
                });
            }
        }
        next_modules.push_back({
            .id = record.id,
            .prototype_id = prototype.id,
            .parent_module_id = record.parent_module_index
                ? std::optional<std::size_t>{module_records_[*record.parent_module_index].id}
                : std::nullopt,
            .parent_terminal_node = record.parent_terminal_node,
            .transform = record.transform,
            .root_position = record.transform.translation,
            .physiological_age = record.physiological_age,
            .fully_grown_age = record.fully_grown_age,
            .direct_light_exposure = direct_light[module],
            .accumulated_light = accumulated[module],
            .vigor = module_vigor[module],
            .growth_rate = module_growth[module],
            .collision_sphere = geometries[module].sphere,
            .segments = {.offset = range_offset, .count = prototype.segments.size()},
        });
    }

    snapshot_modules_ = std::move(next_modules);
    snapshot_segments_ = std::move(next_segments);
    snapshot_terminals_ = std::move(next_terminals);
    if (include_flow_diagnostics) return rebuild_flow_diagnostics(conduit);
    return {};
}

namespace {

GrowthError invalid_input(std::string message)
{
    return {.code = GrowthError::Code::InvalidInput, .message = std::move(message)};
}

GrowthError invalid_prototype(std::string message)
{
    return {.code = GrowthError::Code::InvalidPrototype, .message = std::move(message)};
}

float dot(Vec3 left, Vec3 right)
{
    return left.x * right.x + left.y * right.y + left.z * right.z;
}

Vec3 cross(Vec3 left, Vec3 right)
{
    return {left.y * right.z - left.z * right.y,
            left.z * right.x - left.x * right.z,
            left.x * right.y - left.y * right.x};
}

Vec3 rotate_about_axis(Vec3 value, Vec3 axis, float angle)
{
    axis = normalize(axis);
    return add(add(scale(value, std::cos(angle)), scale(cross(axis, value), std::sin(angle))),
               scale(axis, dot(axis, value) * (1.0F - std::cos(angle))));
}

RigidTransform rotated_transform(const RigidTransform& transform, Vec3 axis, float angle)
{
    auto result = transform;
    result.x_axis = rotate_about_axis(result.x_axis, axis, angle);
    result.y_axis = rotate_about_axis(result.y_axis, axis, angle);
    result.z_axis = rotate_about_axis(result.z_axis, axis, angle);
    return result;
}

EulerAngles euler_angles(const RigidTransform& transform)
{
    const float y = std::asin(std::clamp(-transform.x_axis.z, -1.0F, 1.0F));
    const float y_cosine = std::cos(y);
    if (std::abs(y_cosine) > kEpsilon) {
        return {
            .x = std::atan2(transform.y_axis.z, transform.z_axis.z),
            .y = y,
            .z = std::atan2(transform.x_axis.y, transform.x_axis.x),
        };
    }
    return {
        .x = 0.0F,
        .y = y,
        .z = std::atan2(-transform.y_axis.x, transform.y_axis.y),
    };
}

RigidTransform transform_for_euler(EulerAngles angles, Vec3 translation)
{
    const float x_sine = std::sin(angles.x);
    const float x_cosine = std::cos(angles.x);
    const float y_sine = std::sin(angles.y);
    const float y_cosine = std::cos(angles.y);
    const float z_sine = std::sin(angles.z);
    const float z_cosine = std::cos(angles.z);
    return {
        .x_axis = {z_cosine * y_cosine, z_sine * y_cosine, -y_sine},
        .y_axis = {z_cosine * y_sine * x_sine - z_sine * x_cosine,
                   z_sine * y_sine * x_sine + z_cosine * x_cosine,
                   y_cosine * x_sine},
        .z_axis = {z_cosine * y_sine * x_cosine + z_sine * x_sine,
                   z_sine * y_sine * x_cosine - z_cosine * x_sine,
                   y_cosine * x_cosine},
        .translation = translation,
    };
}

Sphere sphere_for_points(std::span<const Vec3> points)
{
    Vec3 minimum = points.front();
    Vec3 maximum = points.front();
    for (const auto point : points.subspan(1)) {
        minimum.x = std::min(minimum.x, point.x);
        minimum.y = std::min(minimum.y, point.y);
        minimum.z = std::min(minimum.z, point.z);
        maximum.x = std::max(maximum.x, point.x);
        maximum.y = std::max(maximum.y, point.y);
        maximum.z = std::max(maximum.z, point.z);
    }
    const Vec3 center = scale(add(minimum, maximum), 0.5F);
    float radius = 0.0F;
    for (const auto point : points) radius = std::max(radius, distance(center, point));
    return {.center = center, .radius = radius};
}

Result<ModuleGeometry> build_geometry(const BranchModulePrototype& prototype,
                                      const PlantTypeParameters& plant_type,
                                      const RigidTransform& transform, float physiological_age)
{
    auto current = make_growth_snapshot(prototype, plant_type, physiological_age);
    auto mature_age = fully_grown_age(prototype, plant_type);
    if (!current) return std::unexpected(current.error());
    if (!mature_age) return std::unexpected(mature_age.error());
    auto mature = make_growth_snapshot(prototype, plant_type, *mature_age);
    if (!mature) return std::unexpected(mature.error());

    const Vec3 local_root = prototype.nodes[prototype.root_node].position;
    const auto world_point = [&](Vec3 point) { return transform_point(transform, subtract(point, local_root)); };
    ModuleGeometry geometry;
    geometry.current_segments.resize(prototype.segments.size());
    geometry.mature_segments.resize(prototype.segments.size());
    geometry.current_nodes.assign(prototype.nodes.size(), transform.translation);
    geometry.mature_nodes.assign(prototype.nodes.size(), transform.translation);
    std::vector<Vec3> developed_points{Vec3{}};
    for (auto segment : current->segments) {
        developed_points.push_back(subtract(segment.child_position, local_root));
        segment.parent_position = world_point(segment.parent_position);
        segment.child_position = world_point(segment.child_position);
        geometry.current_segments[segment.source_segment_id] = segment;
    }
    for (auto segment : mature->segments) {
        segment.parent_position = world_point(segment.parent_position);
        segment.child_position = world_point(segment.child_position);
        geometry.mature_segments[segment.source_segment_id] = segment;
        geometry.mature_nodes[prototype.segments[segment.source_segment_id].child_node] = segment.child_position;
    }
    std::queue<std::size_t> nodes;
    nodes.push(prototype.root_node);
    while (!nodes.empty()) {
        const auto node = nodes.front();
        nodes.pop();
        for (const auto segment_index : prototype.child_segments_by_node[node]) {
            const auto& segment = prototype.segments[segment_index];
            geometry.current_nodes[segment.child_node] = geometry.current_segments[segment_index]
                ? geometry.current_segments[segment_index]->child_position
                : geometry.current_nodes[node];
            nodes.push(segment.child_node);
        }
    }
    const Sphere local_sphere = sphere_for_points(developed_points);
    geometry.sphere = {
        .center = transform_point(transform, local_sphere.center),
        .radius = local_sphere.radius,
    };
    return geometry;
}

float segment_diameter_maturity(const BranchModulePrototype& prototype,
                                const BranchSegment& segment,
                                float module_physiological_age)
{
    const float segment_age = std::max(
        0.0F, module_physiological_age - prototype.nodes[segment.parent_node].physiological_age);
    return segment.inverse_remaining_diameter_age <= 0.0F
        ? 1.0F
        : std::clamp(segment_age * segment.inverse_remaining_diameter_age, 0.0F, 1.0F);
}

OrientedChild orient_child(const RigidTransform& inherited, Vec3 translation,
                           Vec3 terminal_tangent, const BranchModulePrototype& child_prototype,
                           Sphere mature_local_sphere, const PlantTypeParameters& plant_type,
                           std::span<const Sphere> existing_spheres)
{
    const auto root_segment = child_prototype.child_segments_by_node[child_prototype.root_node].front();
    const Vec3 inherited_root_direction = normalize(
        transform_direction(inherited, child_prototype.segments[root_segment].direction));
    terminal_tangent = normalize(terminal_tangent);
    const float alignment = std::clamp(dot(inherited_root_direction, terminal_tangent), -1.0F, 1.0F);
    Vec3 rotation_axis = cross(inherited_root_direction, terminal_tangent);
    float rotation_axis_length = length(rotation_axis);
    if (rotation_axis_length <= kEpsilon && alignment < 0.0F) {
        rotation_axis = cross(inherited_root_direction, inherited.x_axis);
        rotation_axis_length = length(rotation_axis);
        if (rotation_axis_length <= kEpsilon) {
            rotation_axis = cross(inherited_root_direction, inherited.y_axis);
            rotation_axis_length = length(rotation_axis);
        }
    }
    RigidTransform baseline = inherited;
    if (rotation_axis_length > kEpsilon) {
        baseline = rotated_transform(inherited, scale(rotation_axis, 1.0F / rotation_axis_length),
                                     std::acos(alignment));
    }

    const auto sphere_for_transform = [&](RigidTransform transform) {
        return Sphere{.center = transform_point(transform, mature_local_sphere.center),
                      .radius = mature_local_sphere.radius};
    };
    auto scored = [&](RigidTransform transform) {
        transform.translation = translation;
        const float collisions = collision_measure(sphere_for_transform(transform), existing_spheres);
        const float tropism = orientation_tropism_cost(plant_type.tropism_angle, transform.z_axis,
                                                       {0.0F, 0.0F, 1.0F});
        return orientation_distribution_cost(collisions, 1.0F, tropism, plant_type.tropism_weight);
    };

    RigidTransform best = baseline;
    best.translation = translation;
    EulerAngles best_angles = euler_angles(best);
    float best_cost = scored(best);
    for (int iteration = 0; iteration < kOrientationIterations; ++iteration) {
        // Paper: rho perturbations, interpreting DirectX coordinates as Y-up and plant space as Z-up.
        const std::array candidates{
            EulerAngles{best_angles.x + kOrientationStep, best_angles.y, best_angles.z},
            EulerAngles{best_angles.x - kOrientationStep, best_angles.y, best_angles.z},
            EulerAngles{best_angles.x, best_angles.y + kOrientationStep, best_angles.z},
            EulerAngles{best_angles.x, best_angles.y - kOrientationStep, best_angles.z},
        };
        bool improved = false;
        for (const auto candidate_angles : candidates) {
            const RigidTransform candidate = transform_for_euler(candidate_angles, translation);
            const float cost = scored(candidate);
            if (cost + kEpsilon < best_cost) {
                best = candidate;
                best_angles = candidate_angles;
                best_cost = cost;
                improved = true;
            }
        }
        if (!improved) break;
    }
    return {.transform = best, .mature_sphere = sphere_for_transform(best)};
}

} // namespace
} // namespace toi::growth
