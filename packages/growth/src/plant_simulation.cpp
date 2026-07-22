#include "toi/growth/growth.hpp"

#include "plant_conduit.hpp"

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

struct OrientedChild {
    RigidTransform transform;
    Sphere mature_sphere;
};

struct EulerAngles {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
};

// One Borchert-Honda fork decision, shared by the canonical module-vigor pass
// and the conduit flow diagnostic so the lateral-allocation policy cannot
// drift between them: split available vigor between the main continuation and
// the lateral group, then divide the lateral share proportionally by light,
// falling back to an equal division when the group carries no light.
struct ForkVigorSplit {
    float main = 0.0F;
    float lateral_total = 0.0F;
    float lateral_light = 0.0F;
    std::size_t lateral_count = 0;

    [[nodiscard]] float lateral_share(float light) const
    {
        return lateral_light > kEpsilon ? lateral_total * light / lateral_light
                                        : lateral_total / static_cast<float>(lateral_count);
    }
};

[[nodiscard]] ForkVigorSplit fork_vigor_split(float available_vigor, float main_light, float lateral_light,
                                              std::size_t lateral_count, float apical_control)
{
    const auto split = split_vigor(available_vigor, main_light, lateral_light, apical_control);
    return {
        .main = split.main_axis,
        .lateral_total = split.lateral_axis,
        .lateral_light = lateral_light,
        .lateral_count = lateral_count,
    };
}

[[nodiscard]] float current_apical_control(const PlantTypeParameters& plant_type, float plant_age)
{
    return parameter_for_plant_age(plant_type.apical_control, plant_type.mature_apical_control,
                                   plant_type.flowering_age, plant_age);
}

[[nodiscard]] GrowthError invalid_input(std::string message);
[[nodiscard]] GrowthError invalid_prototype(std::string message);
[[nodiscard]] float dot(Vec3 left, Vec3 right);
[[nodiscard]] Vec3 cross(Vec3 left, Vec3 right);
[[nodiscard]] Vec3 rotate_about_axis(Vec3 value, Vec3 axis, float angle);
[[nodiscard]] RigidTransform rotated_transform(const RigidTransform& transform, Vec3 axis, float angle);
[[nodiscard]] EulerAngles euler_angles(const RigidTransform& transform);
[[nodiscard]] RigidTransform transform_for_euler(EulerAngles angles, Vec3 translation);
[[nodiscard]] Sphere sphere_for_points(std::span<const Vec3> points);
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
    auto rebuilt = simulation.rebuild_snapshot();
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
        auto current = next.rebuild_snapshot();
        if (!current) {
            return std::unexpected(current.error());
        }
        const std::size_t attachment_cohort_size = next.module_records_.size();
        std::vector<float> preintegration_vigor;
        preintegration_vigor.reserve(attachment_cohort_size);
        std::vector<Sphere> orientation_spheres;
        orientation_spheres.reserve(next.snapshot_modules_.size() + next.snapshot_terminals_.size());
        for (const auto& module : next.snapshot_modules_) {
            preintegration_vigor.push_back(module.vigor);
            orientation_spheres.push_back(module.collision_sphere);
        }

        std::vector<std::size_t> crossed_parent_indices;
        crossed_parent_indices.reserve(attachment_cohort_size);
        for (std::size_t module_index = 0; module_index < attachment_cohort_size; ++module_index) {
            auto& module = next.module_records_[module_index];
            const float old_age = module.physiological_age;
            module.physiological_age = std::min(
                module.fully_grown_age,
                physiological_age_euler_step(
                    old_age, next.snapshot_modules_[module_index].growth_rate, timestep));
            if (old_age < module.fully_grown_age &&
                module.physiological_age >= module.fully_grown_age) {
                crossed_parent_indices.push_back(module_index);
            }
        }

        auto attached = next.attach_crossed_modules(
            crossed_parent_indices, preintegration_vigor, std::move(orientation_spheres));
        if (!attached) {
            return std::unexpected(attached.error());
        }

        next.plant_age_ += timestep;
        auto committed = next.rebuild_snapshot();
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
        .attachment_events = snapshot_attachment_events_,
    };
}

Result<void> PlantSimulation::attach_crossed_modules(
    std::span<const std::size_t> crossed_parent_indices,
    std::span<const float> preintegration_vigor,
    std::vector<Sphere> orientation_spheres)
{
    if (crossed_parent_indices.empty()) {
        return {};
    }

    auto crossing_state = rebuild_snapshot();
    if (!crossing_state) {
        return crossing_state;
    }
    const float determinacy = parameter_for_plant_age(
        plant_type_.determinacy, plant_type_.mature_determinacy,
        plant_type_.flowering_age, plant_age_);
    const float apical_control = current_apical_control(plant_type_, plant_age_);
    module_records_.reserve(module_records_.size() + snapshot_terminals_.size());
    snapshot_attachment_events_.reserve(snapshot_terminals_.size());
    orientation_spheres.reserve(orientation_spheres.size() + snapshot_terminals_.size());

    for (const std::size_t parent_index : crossed_parent_indices) {
        const std::size_t parent_id = module_records_[parent_index].id;
        const std::size_t parent_prototype_index = module_records_[parent_index].prototype_index;
        const RigidTransform parent_transform = module_records_[parent_index].transform;
        const std::size_t selected_id = nearest_morphospace_prototype(
            apical_control,
            vigor_scaled_determinacy(determinacy, preintegration_vigor[parent_index]));
        const auto selected = std::ranges::find(
            prepared_prototypes_, selected_id, &BranchModulePrototype::id);
        const std::size_t selected_index =
            static_cast<std::size_t>(selected - prepared_prototypes_.begin());
        auto selected_mature_age = fully_grown_age(*selected, plant_type_);
        if (!selected_mature_age) {
            return std::unexpected(selected_mature_age.error());
        }

        const auto& ordered_terminal_nodes =
            prototype_orientation_data_[parent_prototype_index].ordered_terminal_nodes;
        for (const std::size_t terminal_node : ordered_terminal_nodes) {
            const auto terminal = std::ranges::find_if(snapshot_terminals_, [&](const auto& value) {
                return value.module_id == parent_id && value.terminal_node == terminal_node;
            });
            if (terminal == snapshot_terminals_.end() || terminal->child_module_id ||
                terminal->vigor <= kMinimumModuleVigor) {
                continue;
            }

            const std::size_t child_id = next_module_id_++;
            const OrientedChild oriented = orient_child(
                parent_transform, terminal->position, terminal->tangent, *selected,
                prototype_orientation_data_[selected_index].mature_sphere,
                plant_type_, orientation_spheres);
            module_records_.push_back({
                .id = child_id,
                .prototype_index = selected_index,
                .parent_module_index = parent_index,
                .parent_terminal_node = terminal_node,
                .transform = oriented.transform,
                .physiological_age = 0.0F,
                .fully_grown_age = *selected_mature_age,
                .developed_diameters = std::vector<float>(
                    selected->segments.size(), plant_type_.terminal_thickness * kCentimetersToMeters),
                .diagnostics_active = false,
            });
            snapshot_attachment_events_.push_back({
                .child_module_id = child_id,
                .parent_module_id = parent_id,
                .parent_terminal_node = terminal_node,
                .prototype_id = selected_id,
            });
            orientation_spheres.push_back(oriented.mature_sphere);
        }
    }
    return {};
}

Result<void> PlantSimulation::rebuild_snapshot()
{
    const std::size_t module_count = module_records_.size();
    std::vector<detail::ConduitModuleView> conduit_modules;
    conduit_modules.reserve(module_count);
    for (auto& module : module_records_) {
        conduit_modules.push_back({
            .prototype = &prepared_prototypes_[module.prototype_index],
            .parent_module_index = module.parent_module_index,
            .parent_terminal_node = module.parent_terminal_node,
            .physiological_age = module.physiological_age,
            .developed_diameters = module.developed_diameters,
        });
    }
    auto conduit = detail::PlantConduit::develop(
        conduit_modules, plant_type_.terminal_thickness * kCentimetersToMeters);
    if (!conduit) {
        return std::unexpected(conduit.error());
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

    // Pass A: basipetal light accumulation over the module tree.
    // Paper: Q_accumulated(u), summed tip-to-root across module attachments.
    std::vector<float> accumulated(module_count, 0.0F);
    for (std::size_t reverse = module_count; reverse > 0; --reverse) {
        const std::size_t module = reverse - 1;
        const auto& record = module_records_[module];
        if (!record.diagnostics_active) {
            continue;
        }
        accumulated[module] += direct_light[module];
        if (const auto parent = record.parent_module_index) {
            accumulated[*parent] += accumulated[module];
        }
    }

    // Pass B: acropetal vigor distribution over the module tree, Eq. 2 applied at
    // each module intersection. The flux is conserved -- a module's vigor divides
    // entirely among its child modules. Maximum module vigor never truncates it
    // here; it only normalizes the Eq. 5 growth rate and morphospace D'.
    std::vector<float> module_vigor(module_count, 0.0F);
    std::vector<float> module_growth(module_count, 0.0F);
    if (!module_records_.empty()) {
        // Paper: v̄(u_root) = min(Q_accumulated(u_root), v̄_rootmax).
        module_vigor[0] = std::min(accumulated[0], plant_type_.root_max_vigor);
    }
    const float apical_control = current_apical_control(plant_type_, plant_age_);
    for (std::size_t module = 0; module < module_count; ++module) {
        const auto& record = module_records_[module];
        const auto& prototype = prepared_prototypes_[record.prototype_index];
        if (!record.diagnostics_active) {
            continue;
        }
        auto rate = growth_rate(plant_type_, {
            .vigor = module_vigor[module],
            .min_vigor = kMinimumModuleVigor,
            .max_vigor = kMaximumModuleVigor,
        });
        if (!rate) {
            return std::unexpected(rate.error());
        }
        module_growth[module] = *rate;

        std::optional<std::size_t> main_child;
        std::vector<std::size_t> lateral_children;
        float lateral_light = 0.0F;
        for (const auto terminal : prototype.terminal_nodes) {
            const auto child = conduit->attached_child(module, terminal);
            if (!child || !module_records_[*child].diagnostics_active) {
                continue;
            }
            if (terminal == prototype.main_axis_terminal_node) {
                main_child = *child;
            } else {
                lateral_children.push_back(*child);
                lateral_light += accumulated[*child];
            }
        }

        if (lateral_children.empty()) {
            if (main_child) {
                module_vigor[*main_child] = module_vigor[module];
            }
            continue;
        }
        // Paper: u_m is the main-axis child and the remaining children form u_l.
        // Without a main-axis child Eq. 2 leaves the main branch zero light, so the
        // whole flux belongs to the lateral group.
        const auto fork = main_child
            ? fork_vigor_split(module_vigor[module], accumulated[*main_child], lateral_light,
                               lateral_children.size(), apical_control)
            : ForkVigorSplit{
                  .main = 0.0F,
                  .lateral_total = module_vigor[module],
                  .lateral_light = lateral_light,
                  .lateral_count = lateral_children.size(),
              };
        if (main_child) {
            module_vigor[*main_child] = fork.main;
        }
        for (const auto child : lateral_children) {
            module_vigor[child] = fork.lateral_share(accumulated[child]);
        }
    }

    // Pass C: module-scale terminal vigor for mature modules. Paper: terminal light
    // is the module's own direct exposure divided equally, q(n_i) = Q(u)/#n, and the
    // module's vigor is distributed over that light. Its result gates attachment; it
    // is module-scale v, distinct from the plant-scale v̄ computed above.
    std::vector<std::vector<float>> node_vigor(module_count);
    for (std::size_t module = 0; module < module_count; ++module) {
        const auto& record = module_records_[module];
        const auto& prototype = prepared_prototypes_[record.prototype_index];
        node_vigor[module].assign(prototype.nodes.size(), 0.0F);
        if (!record.diagnostics_active ||
            record.physiological_age + kEpsilon < record.fully_grown_age) {
            continue;
        }

        std::vector<float> node_light(prototype.nodes.size(), 0.0F);
        const float terminal_share =
            direct_light[module] / static_cast<float>(prototype.terminal_nodes.size());
        for (const auto terminal : prototype.terminal_nodes) {
            node_light[terminal] = terminal_share;
        }
        const std::function<float(std::size_t)> accumulate_from_node = [&](std::size_t node) {
            float total = node_light[node];
            for (const auto child_segment : prototype.child_segments_by_node[node]) {
                total += accumulate_from_node(prototype.segments[child_segment].child_node);
            }
            node_light[node] = total;
            return total;
        };
        (void)accumulate_from_node(prototype.root_node);

        node_vigor[module][prototype.root_node] = module_vigor[module];
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
                        lateral_light += node_light[prototype.segments[child_segment].child_node];
                        ++lateral_count;
                    }
                }
                const auto fork = fork_vigor_split(node_vigor[module][node], node_light[main_node],
                                                   lateral_light, lateral_count, apical_control);
                node_vigor[module][main_node] = fork.main;
                for (const auto child_segment : children) {
                    if (child_segment == *main_segment) continue;
                    const auto child_node = prototype.segments[child_segment].child_node;
                    node_vigor[module][child_node] = fork.lateral_share(node_light[child_node]);
                }
            }
            for (const auto child_segment : children) {
                queue.push(prototype.segments[child_segment].child_node);
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
        const auto conduit_segments = conduit->module_segments(module);
        for (std::size_t source = 0; source < prototype.segments.size(); ++source) {
            const auto& definition = prototype.segments[source];
            const auto& current = geometries[module].current_segments[source];
            const auto& conduit_segment = conduit_segments[source];
            next_segments.push_back({
                .module_id = record.id,
                .source_segment_id = source,
                .parent_position = current ? current->parent_position : geometries[module].current_nodes[definition.parent_node],
                .child_position = current ? current->child_position : geometries[module].current_nodes[definition.child_node],
                .mature_parent_position = geometries[module].mature_segments[source].parent_position,
                .mature_child_position = geometries[module].mature_segments[source].child_position,
                .diameter = conduit_segment.diameter,
                .state = current ? current->state : SegmentState::Growing,
                .main_continuation_segment = conduit_segment.main_continuation_segment,
            });
        }

        if (record.physiological_age + kEpsilon >= record.fully_grown_age) {
            for (const auto terminal : prototype.terminal_nodes) {
                const auto incoming = prototype.incoming_segment_by_node[terminal];
                const auto& segment = next_segments[range_offset + *incoming];
                const auto child = conduit->attached_child(module, terminal);
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
