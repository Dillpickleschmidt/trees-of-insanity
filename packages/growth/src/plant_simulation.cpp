#include "toi/growth/growth.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
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

[[nodiscard]] GrowthError invalid_input(std::string message);
[[nodiscard]] GrowthError invalid_prototype(std::string message);
[[nodiscard]] float dot(Vec3 left, Vec3 right);
[[nodiscard]] Vec3 cross(Vec3 left, Vec3 right);
[[nodiscard]] Vec3 rotate_about_axis(Vec3 value, Vec3 axis, float angle);
[[nodiscard]] RigidTransform rotated_transform(const RigidTransform& transform, Vec3 axis, float angle);
[[nodiscard]] Sphere sphere_for_points(std::span<const Vec3> points);
[[nodiscard]] float segment_diameter_maturity(const BranchModulePrototype& prototype,
                                              const BranchSegment& segment,
                                              float module_physiological_age);
[[nodiscard]] Result<ModuleGeometry> build_geometry(const BranchModulePrototype& prototype,
                                                    const PlantTypeParameters& plant_type,
                                                    const RigidTransform& transform,
                                                    float physiological_age);
[[nodiscard]] std::vector<float> dynamic_pipe_factors(
    const BranchModulePrototype& prototype,
    std::span<const std::optional<std::size_t>> child_module_by_node,
    std::span<const float> child_root_supplies);
[[nodiscard]] RigidTransform orient_child(const RigidTransform& inherited, Vec3 translation,
                                          const BranchModulePrototype& child_prototype,
                                          const PlantTypeParameters& plant_type,
                                          std::span<const Sphere> existing_spheres);

} // namespace

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
        .diagnostics_active = true,
    });
    simulation.snapshot_attachment_events_.clear();
    simulation.snapshot_flows_.clear();
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
        auto current = next.rebuild_snapshot(true);
        if (!current) {
            return std::unexpected(current.error());
        }
        const auto precommit_root = std::ranges::find(next.snapshot_modules_, 0, &PlantModuleSnapshot::id);
        const float precommit_root_vigor = precommit_root->vigor;
        std::vector<Sphere> precommit_spheres;
        precommit_spheres.reserve(next.snapshot_modules_.size());
        for (const auto& module : next.snapshot_modules_) {
            precommit_spheres.push_back(module.collision_sphere);
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

            const auto& root_prototype = next.prepared_prototypes_[root_record->prototype_index];
            for (const auto terminal_node : root_prototype.terminal_nodes) {
                const auto terminal = std::ranges::find_if(next.snapshot_terminals_, [&](const auto& value) {
                    return value.module_id == 0 && value.terminal_node == terminal_node;
                });
                if (terminal == next.snapshot_terminals_.end() || terminal->child_module_id ||
                    terminal->vigor <= kMinimumModuleVigor) {
                    continue;
                }
                const std::size_t child_id = next.next_module_id_++;
                const RigidTransform child_transform = orient_child(
                    root_record->transform, terminal->position, *selected, next.plant_type_, precommit_spheres);
                next.module_records_.push_back({
                    .id = child_id,
                    .prototype_index = selected_index,
                    .parent_module_index = root_record_index,
                    .parent_terminal_node = terminal_node,
                    .transform = child_transform,
                    .physiological_age = 0.0F,
                    .fully_grown_age = *selected_mature_age,
                    .diagnostics_active = false,
                });
                next.snapshot_attachment_events_.push_back({
                    .child_module_id = child_id,
                    .parent_module_id = 0,
                    .parent_terminal_node = terminal_node,
                    .prototype_id = selected_id,
                });
            }
        }

        next.plant_age_ += timestep;
        auto committed = next.rebuild_snapshot(false);
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

PlantSnapshot PlantSimulation::snapshot() const
{
    return {
        .plant_age = plant_age_,
        .modules = snapshot_modules_,
        .segments = snapshot_segments_,
        .mature_terminals = snapshot_terminals_,
        .flow_paths = snapshot_flows_,
        .attachment_events = snapshot_attachment_events_,
    };
}

Result<void> PlantSimulation::rebuild_snapshot(bool emit_flows)
{
    const std::size_t module_count = module_records_.size();
    std::vector<std::size_t> module_node_offsets(module_count + 1, 0);
    for (std::size_t module = 0; module < module_count; ++module) {
        module_node_offsets[module + 1] = module_node_offsets[module] +
            prepared_prototypes_[module_records_[module].prototype_index].nodes.size();
    }
    std::vector<std::optional<std::size_t>> child_module_by_node(module_node_offsets.back());
    for (std::size_t child = 1; child < module_count; ++child) {
        const auto& record = module_records_[child];
        child_module_by_node[module_node_offsets[*record.parent_module_index] +
                             *record.parent_terminal_node] = child;
    }

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

    std::vector<std::vector<float>> pipe_factors(module_count);
    std::vector<float> root_supplies(module_count, 1.0F);
    for (std::size_t reverse = module_count; reverse > 0; --reverse) {
        const std::size_t module = reverse - 1;
        const auto& prototype = prepared_prototypes_[module_records_[module].prototype_index];
        pipe_factors[module] = dynamic_pipe_factors(
            prototype,
            std::span<const std::optional<std::size_t>>(child_module_by_node).subspan(
                module_node_offsets[module], prototype.nodes.size()),
            root_supplies);
        float root_sum = 0.0F;
        for (const auto segment_index : prototype.child_segments_by_node[prototype.root_node]) {
            const auto& segment = prototype.segments[segment_index];
            const float maturity = segment_diameter_maturity(
                prototype, segment, module_records_[module].physiological_age);
            const float target = pipe_factors[module][segment_index];
            const float developed = 1.0F + (target - 1.0F) * maturity;
            root_sum += developed * developed;
        }
        root_supplies[module] = root_sum > 0.0F ? std::sqrt(root_sum) : 1.0F;
    }

    std::vector<PlantModuleSnapshot> next_modules;
    std::vector<PlantSegmentSnapshot> next_segments;
    std::vector<MatureTerminalSnapshot> next_terminals;
    std::vector<PlantFlowPath> next_flows;
    next_modules.reserve(module_count);
    const float terminal_thickness = plant_type_.terminal_thickness * kCentimetersToMeters;
    const float root_light_total = accumulated.empty() ? 0.0F : accumulated.front();
    const float root_vigor_total = root_vigor_budget;

    for (std::size_t module = 0; module < module_count; ++module) {
        const auto& record = module_records_[module];
        const auto& prototype = prepared_prototypes_[record.prototype_index];
        const auto range_offset = next_segments.size();
        for (std::size_t source = 0; source < prototype.segments.size(); ++source) {
            const auto& definition = prototype.segments[source];
            const auto& current = geometries[module].current_segments[source];
            const float maturity = segment_diameter_maturity(
                prototype, definition, record.physiological_age);
            const float target_diameter = pipe_factors[module][source] * terminal_thickness;
            const float diameter = terminal_thickness + (target_diameter - terminal_thickness) * maturity;
            std::optional<std::size_t> continuation;
            const auto main = prototype.main_child_segment_by_node[definition.child_node];
            if (main) continuation = range_offset + *main;
            next_segments.push_back({
                .module_id = record.id,
                .source_segment_id = source,
                .parent_position = current ? current->parent_position : geometries[module].current_nodes[definition.parent_node],
                .child_position = current ? current->child_position : geometries[module].current_nodes[definition.child_node],
                .mature_parent_position = geometries[module].mature_segments[source].parent_position,
                .mature_child_position = geometries[module].mature_segments[source].child_position,
                .diameter = diameter,
                .target_diameter = target_diameter,
                .state = current ? current->state : SegmentState::Growing,
                .main_continuation_segment = continuation,
            });
            if (emit_flows && current && record.diagnostics_active) {
                const float light_amount = node_light[module][definition.child_node];
                const float vigor_amount = node_vigor[module][definition.child_node];
                const Vec3 tangent = normalize(subtract(current->child_position, current->parent_position));
                if (light_amount > kEpsilon && root_light_total > kEpsilon) {
                    next_flows.push_back({
                        .kind = FlowKind::AccumulatedLight,
                        .module_id = record.id,
                        .source_segment_id = source,
                        .start = current->child_position,
                        .end = current->parent_position,
                        .tangent = scale(tangent, -1.0F),
                        .host_radius = diameter * 0.5F,
                        .amount = light_amount,
                        .root_total = root_light_total,
                        .fraction = std::clamp(light_amount / root_light_total, 0.0F, 1.0F),
                    });
                }
                if (vigor_amount > kEpsilon && root_vigor_total > kEpsilon) {
                    next_flows.push_back({
                        .kind = FlowKind::Vigor,
                        .module_id = record.id,
                        .source_segment_id = source,
                        .start = current->parent_position,
                        .end = current->child_position,
                        .tangent = tangent,
                        .host_radius = diameter * 0.5F,
                        .amount = vigor_amount,
                        .root_total = root_vigor_total,
                        .fraction = std::clamp(vigor_amount / root_vigor_total, 0.0F, 1.0F),
                    });
                }
            }
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
                    .host_radius = segment.target_diameter * 0.5F,
                    .vigor = node_vigor[module][terminal],
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
    if (emit_flows) {
        snapshot_flows_ = std::move(next_flows);
    }
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

Sphere sphere_for_points(std::span<const Vec3> points)
{
    Vec3 center;
    for (const auto point : points) center = add(center, point);
    center = scale(center, 1.0F / static_cast<float>(points.size()));
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
    for (auto segment : current->segments) {
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
    std::vector<Vec3> developed{transform.translation};
    for (const auto& segment : geometry.current_segments) {
        if (segment) developed.push_back(segment->child_position);
    }
    geometry.sphere = sphere_for_points(developed);
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

std::vector<float> dynamic_pipe_factors(
    const BranchModulePrototype& prototype,
    std::span<const std::optional<std::size_t>> child_module_by_node,
    std::span<const float> child_root_supplies)
{
    std::vector<float> factors(prototype.segments.size(), -1.0F);
    const auto factor = [&](auto&& self, std::size_t segment_index) -> float {
        if (factors[segment_index] >= 0.0F) return factors[segment_index];
        const auto& segment = prototype.segments[segment_index];
        if (const auto child = child_module_by_node[segment.child_node]) {
            return factors[segment_index] = child_root_supplies[*child];
        }
        float sum = 0.0F;
        for (const auto next : prototype.child_segments_by_node[segment.child_node]) {
            const float child_factor = self(self, next);
            sum += child_factor * child_factor;
        }
        return factors[segment_index] = sum > 0.0F ? std::sqrt(sum) : 1.0F;
    };
    for (std::size_t index = 0; index < prototype.segments.size(); ++index) (void)factor(factor, index);
    return factors;
}

RigidTransform orient_child(const RigidTransform& inherited, Vec3 translation,
                            const BranchModulePrototype& child_prototype,
                            const PlantTypeParameters& plant_type, std::span<const Sphere> existing_spheres)
{
    auto scored = [&](RigidTransform transform) {
        transform.translation = translation;
        const auto geometry = build_geometry(child_prototype, plant_type, transform,
                                             child_prototype.max_physiological_age);
        const Sphere sphere = geometry->sphere;
        const float collisions = collision_measure(sphere, existing_spheres);
        const float tropism = orientation_tropism_cost(plant_type.tropism_angle, transform.z_axis,
                                                       {0.0F, 0.0F, 1.0F});
        return orientation_distribution_cost(collisions, 1.0F, tropism, plant_type.tropism_weight);
    };

    RigidTransform best = inherited;
    best.translation = translation;
    float best_cost = scored(best);
    for (int iteration = 0; iteration < kOrientationIterations; ++iteration) {
        const std::array candidates{
            rotated_transform(best, best.x_axis, kOrientationStep),
            rotated_transform(best, best.x_axis, -kOrientationStep),
            rotated_transform(best, best.z_axis, kOrientationStep),
            rotated_transform(best, best.z_axis, -kOrientationStep),
        };
        bool improved = false;
        for (auto candidate : candidates) {
            candidate.translation = translation;
            const float cost = scored(candidate);
            if (cost + kEpsilon < best_cost) {
                best = candidate;
                best_cost = cost;
                improved = true;
            }
        }
        if (!improved) break;
    }
    return best;
}

} // namespace
} // namespace toi::growth
