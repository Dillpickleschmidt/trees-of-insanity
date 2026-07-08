#include "toi/plant/plant.hpp"

#include <array>
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <utility>
#include <vector>

namespace toi::plant {
namespace {

using growth::add;
using growth::BranchModulePrototype;
using growth::distance;
using growth::GrowthSnapshot;
using growth::normalize;
using growth::PlantTypeParameters;
using growth::scale;
using growth::subtract;

// Fixed simulation step. Plant age p_t and each module age a_u are integrated with
// forward Euler at this step. Fixed (not adaptive) so develop_plant(age) trajectories
// share a common prefix as the age slider advances.
constexpr float kSimStep = 1.0F;
constexpr int kMaxSimSteps = 1200;

// Shedding / attachment threshold v̄_min is not in Table 4; use a fraction of root
// max vigor so module count stays bounded (~1/kShedVigorFraction) across all presets.
// NOTE(decision): revisit if the paper's absolute v̄_min surfaces.
constexpr float kShedVigorFraction = 0.02F;

// Morphospace 3x3 grid cell centers (ADR-0013). D increases with column, apical
// control (lambda) increases with row; prototype index i -> (row=i/3, col=i%3).
constexpr std::array<float, 3> kMorphospaceLevels{1.0F / 6.0F, 0.5F, 5.0F / 6.0F};

// Module orientation optimization (SS 5.2.3 / A.1): coordinate descent over +/- steps
// about the local x and z axes. omega_1 is fixed at 1 (only omega_2 is tabulated).
constexpr int kOrientIterations = 8;
constexpr float kOrientStep = 0.15F;
constexpr float kOrientCollisionWeight = 1.0F; // omega_1
constexpr Vec3 kWorldUp{0.0F, 0.0F, 1.0F};

// Senescence ramp: once p_t >= p_max, root vigor interpolates to zero over this many
// steps of plant age (paper: "constant step size").
constexpr float kSenescenceRampAge = 50.0F;

// Number of root-maturation times a fully-vigorous module would reach over the plant's
// lifespan. Used to derive a per-species age time-scale that reconciles the unscaled
// growth rates (g_p) with the import-scaled prototype geometry, so every species
// develops across its slider range. NOTE(decision): v1 normalization; absolute
// plant-scale time units are unspecified in the paper.
constexpr float kLifespanMaturities = 8.0F;

struct Sphere {
    Vec3 center;
    float radius = 0.0F;
};

struct BoundingResult {
    Sphere sphere;
    bool valid = false;
};

[[nodiscard]] PlantError invalid_input(std::string message)
{
    return {PlantError::Code::InvalidInput, std::move(message)};
}

[[nodiscard]] Vec3 cross(Vec3 a, Vec3 b)
{
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

[[nodiscard]] float dot(Vec3 a, Vec3 b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

// Rodrigues rotation of v about unit axis k by angle.
[[nodiscard]] Vec3 rotate_about(Vec3 v, Vec3 k, float angle)
{
    const float c = std::cos(angle);
    const float s = std::sin(angle);
    return add(add(scale(v, c), scale(cross(k, v), s)), scale(k, dot(k, v) * (1.0F - c)));
}

// One module's orthonormal frame placed in plant world space. Local module-space
// points are expressed relative to the prototype root node.
struct Frame {
    Vec3 origin;
    Vec3 x{1.0F, 0.0F, 0.0F};
    Vec3 y{0.0F, 1.0F, 0.0F};
    Vec3 z{0.0F, 0.0F, 1.0F};
};

[[nodiscard]] Frame rotate_frame(const Frame& frame, Vec3 local_axis, float angle)
{
    // local_axis is one of the frame's basis vectors (already in world space).
    Frame rotated = frame;
    rotated.x = normalize(rotate_about(frame.x, local_axis, angle));
    rotated.y = normalize(rotate_about(frame.y, local_axis, angle));
    rotated.z = normalize(rotate_about(frame.z, local_axis, angle));
    return rotated;
}

[[nodiscard]] Vec3 to_world(const Frame& frame, Vec3 local, Vec3 local_root)
{
    const Vec3 relative = subtract(local, local_root);
    return add(frame.origin,
               add(add(scale(frame.x, relative.x), scale(frame.y, relative.y)), scale(frame.z, relative.z)));
}

// Prepared prototype plus its morphospace seed coordinate.
struct Prototype {
    BranchModulePrototype prepared;
    float seed_lambda = 0.0F;
    float seed_determinacy = 0.0F;
    float mature_age = 0.0F; // a_mature: fully-grown age
};

// Working module during simulation.
struct SimModule {
    std::size_t prototype_index = 0;
    std::size_t parent = kNoParent;
    std::size_t parent_terminal_node = 0;
    Frame frame;
    float age = 0.0F;
    float vigor = 0.0F;
    // Terminal nodes of this module's prototype already carrying a child module.
    std::vector<bool> terminal_occupied;
    // Transient per-step state.
    float light = 1.0F;
    float subtree_light = 0.0F;
    std::vector<std::size_t> children;
};

[[nodiscard]] float shed_threshold(const PlantTypeParameters& plant_type)
{
    return kShedVigorFraction * plant_type.root_max_vigor;
}

[[nodiscard]] float sphere_volume(float radius)
{
    return 4.0F / 3.0F * std::numbers::pi_v<float> * radius * radius * radius;
}

[[nodiscard]] float sphere_overlap_volume(float d, float r1, float r2)
{
    if (d >= r1 + r2) {
        return 0.0F;
    }
    if (d <= std::fabs(r1 - r2)) {
        return sphere_volume(std::min(r1, r2));
    }
    const float sum = r1 + r2;
    const float diff = r1 - r2;
    return std::numbers::pi_v<float> * (sum - d) * (sum - d) * (d * d + 2.0F * d * sum - 3.0F * diff * diff) /
           (12.0F * d);
}

// World-space endpoints of a module's grown geometry at a given age.
[[nodiscard]] std::vector<Vec3> module_world_points(const Prototype& prototype,
                                                    const PlantTypeParameters& plant_type, const Frame& frame,
                                                    float age)
{
    std::vector<Vec3> points;
    const Vec3 root = prototype.prepared.nodes[prototype.prepared.root_node].position;
    auto snapshot = growth::make_growth_snapshot(prototype.prepared, plant_type, age);
    if (!snapshot) {
        return points;
    }
    points.reserve(snapshot->segments.size() + 1);
    points.push_back(to_world(frame, root, root)); // module root
    for (const auto& segment : snapshot->segments) {
        points.push_back(to_world(frame, segment.child_position, root));
    }
    return points;
}

[[nodiscard]] BoundingResult bounding_sphere(const std::vector<Vec3>& points)
{
    if (points.empty()) {
        return {};
    }
    Vec3 centroid{};
    for (const Vec3& p : points) {
        centroid = add(centroid, p);
    }
    centroid = scale(centroid, 1.0F / static_cast<float>(points.size()));
    float radius = 0.0F;
    for (const Vec3& p : points) {
        radius = std::max(radius, distance(centroid, p));
    }
    return {.sphere = {.center = centroid, .radius = radius}, .valid = true};
}

// f_collisions(u): overlap of u's bounding sphere with all non-adjacent modules,
// normalized by u's own sphere volume so the measure is scale-invariant (SS eq1 gives
// no units; import scales prototypes). Direct parent/child adjacency is excluded since
// structural attachment is not crowding.
// NOTE(decision): normalization + adjacency exclusion are unspecified in the paper.
[[nodiscard]] float collision_measure(std::size_t index, const Sphere& sphere,
                                      const std::vector<SimModule>& modules, const std::vector<Sphere>& spheres)
{
    const float own_volume = sphere_volume(sphere.radius);
    if (own_volume <= growth::kEpsilon) {
        return 0.0F;
    }
    float total = 0.0F;
    for (std::size_t other = 0; other < modules.size(); ++other) {
        if (other == index) {
            continue;
        }
        if (modules[index].parent == other || modules[other].parent == index) {
            continue;
        }
        const float d = distance(sphere.center, spheres[other].center);
        total += sphere_overlap_volume(d, sphere.radius, spheres[other].radius);
    }
    return total / own_volume;
}

[[nodiscard]] std::size_t select_prototype(float query_lambda, float query_determinacy, std::size_t prototype_count)
{
    const std::size_t count = std::min<std::size_t>(prototype_count, 9);
    std::size_t best = 0;
    float best_distance_sq = std::numeric_limits<float>::max();
    for (std::size_t i = 0; i < count; ++i) {
        const float seed_lambda = kMorphospaceLevels[i / 3];
        const float seed_determinacy = kMorphospaceLevels[i % 3];
        const float dl = query_lambda - seed_lambda;
        const float dd = query_determinacy - seed_determinacy;
        const float distance_sq = dl * dl + dd * dd;
        if (distance_sq < best_distance_sq) {
            best_distance_sq = distance_sq;
            best = i;
        }
    }
    return best;
}

[[nodiscard]] float current_apical_control(const PlantTypeParameters& plant_type, float plant_age)
{
    if (plant_type.mature_apical_control && plant_type.flowering_age > 0.0F && plant_age >= plant_type.flowering_age) {
        return *plant_type.mature_apical_control;
    }
    return plant_type.apical_control;
}

[[nodiscard]] float current_determinacy(const PlantTypeParameters& plant_type, float plant_age)
{
    if (plant_type.mature_determinacy && plant_type.flowering_age > 0.0F && plant_age >= plant_type.flowering_age) {
        return *plant_type.mature_determinacy;
    }
    return plant_type.determinacy;
}

[[nodiscard]] float senescence_factor(const PlantTypeParameters& plant_type, float plant_age)
{
    if (plant_age < plant_type.plant_max_age) {
        return 1.0F;
    }
    const float over = plant_age - plant_type.plant_max_age;
    return std::max(0.0F, 1.0F - over / kSenescenceRampAge);
}

// Basipetal light accumulation (tips -> root) into subtree_light.
void accumulate_light(std::vector<SimModule>& modules)
{
    for (std::size_t reverse = modules.size(); reverse > 0; --reverse) {
        const std::size_t index = reverse - 1;
        SimModule& module = modules[index];
        module.subtree_light = module.light;
        for (const std::size_t child_index : module.children) {
            module.subtree_light += modules[child_index].subtree_light;
        }
    }
}

// Acropetal vigor distribution (root -> tips), Borchert-Honda eq2 applied recursively:
// at each branching point the deepest child axis is "main" and eq2 splits it against the
// combined rest, then the rest is split the same way, so apical control (lambda) applies
// at every level (main vs. lateral, then lateral vs. lateral, ...).
void distribute_vigor(std::vector<SimModule>& modules, const std::vector<Prototype>& prototypes, float lambda,
                      float root_vigor)
{
    if (modules.empty()) {
        return;
    }
    modules.front().vigor = root_vigor;
    for (std::size_t index = 0; index < modules.size(); ++index) {
        if (modules[index].children.empty()) {
            continue;
        }
        // Order children by continuation depth (base node physiological age) descending, so
        // the deepest axis is "main" first at each recursive split.
        const auto& parent_proto = prototypes[modules[index].prototype_index].prepared;
        std::vector<std::size_t> ordered(modules[index].children.begin(), modules[index].children.end());
        std::sort(ordered.begin(), ordered.end(), [&](std::size_t a, std::size_t b) {
            return parent_proto.nodes[modules[a].parent_terminal_node].physiological_age >
                   parent_proto.nodes[modules[b].parent_terminal_node].physiological_age;
        });

        float available = modules[index].vigor;
        for (std::size_t i = 0; i < ordered.size(); ++i) {
            if (i + 1 == ordered.size()) {
                modules[ordered[i]].vigor = available; // last child takes the remainder
                break;
            }
            const float main_light = modules[ordered[i]].subtree_light;
            float rest_light = 0.0F;
            for (std::size_t j = i + 1; j < ordered.size(); ++j) {
                rest_light += modules[ordered[j]].subtree_light;
            }
            const float denominator = lambda * main_light + (1.0F - lambda) * rest_light;
            const float main_vigor =
                denominator <= growth::kEpsilon ? available : available * lambda * main_light / denominator;
            modules[ordered[i]].vigor = main_vigor;
            available -= main_vigor;
        }
    }
}

void rebuild_children(std::vector<SimModule>& modules)
{
    for (SimModule& module : modules) {
        module.children.clear();
    }
    for (std::size_t index = 0; index < modules.size(); ++index) {
        if (modules[index].parent != kNoParent) {
            modules[modules[index].parent].children.push_back(index);
        }
    }
}

// Orientation quality f_distribution = omega_1 * f_collisions + omega_2 * f_tropism.
[[nodiscard]] float orientation_cost(const Prototype& prototype, const PlantTypeParameters& plant_type,
                                     const Frame& frame, const std::vector<Sphere>& spheres,
                                     const std::vector<SimModule>& modules, std::size_t parent_index)
{
    // Evaluate collisions using the module's mature extent so orientation accounts for
    // where the grown module will sit.
    const auto points = module_world_points(prototype, plant_type, frame, prototype.mature_age);
    const auto bounds = bounding_sphere(points);
    float collisions = 0.0F;
    if (bounds.valid) {
        const float own_volume = sphere_volume(bounds.sphere.radius);
        if (own_volume > growth::kEpsilon) {
            for (std::size_t other = 0; other < modules.size(); ++other) {
                if (other == parent_index) {
                    continue;
                }
                const float d = distance(bounds.sphere.center, spheres[other].center);
                collisions += sphere_overlap_volume(d, bounds.sphere.radius, spheres[other].radius) / own_volume;
            }
        }
    }
    // f_tropism = |cos(alpha_tropism) - cos(module axis vs world up)| (SS eq4).
    Vec3 axis = frame.z;
    if (points.size() >= 2) {
        axis = normalize(subtract(points.back(), points.front()));
    }
    const float cos_axis = dot(axis, kWorldUp);
    const float tropism = std::fabs(std::cos(plant_type.tropism_angle) - cos_axis);
    return kOrientCollisionWeight * collisions + plant_type.tropism_weight * tropism;
}

// Coordinate descent over +/- steps about local x and z (SS A.1).
[[nodiscard]] Frame optimize_orientation(const Prototype& prototype, const PlantTypeParameters& plant_type,
                                         Frame frame, const std::vector<Sphere>& spheres,
                                         const std::vector<SimModule>& modules, std::size_t parent_index)
{
    float best_cost = orientation_cost(prototype, plant_type, frame, spheres, modules, parent_index);
    for (int iteration = 0; iteration < kOrientIterations; ++iteration) {
        Frame best_frame = frame;
        bool improved = false;
        const std::array<std::pair<Vec3, float>, 4> candidates{{
            {frame.x, kOrientStep},
            {frame.x, -kOrientStep},
            {frame.z, kOrientStep},
            {frame.z, -kOrientStep},
        }};
        for (const auto& [axis, angle] : candidates) {
            const Frame candidate = rotate_frame(frame, axis, angle);
            const float cost = orientation_cost(prototype, plant_type, candidate, spheres, modules, parent_index);
            if (cost + growth::kEpsilon < best_cost) {
                best_cost = cost;
                best_frame = candidate;
                improved = true;
            }
        }
        if (!improved) {
            break;
        }
        frame = best_frame;
    }
    return frame;
}

[[nodiscard]] Result<std::vector<Prototype>> prepare_prototypes(const import::BranchModulePrototypeLibrary& library,
                                                                const PlantTypeParameters& plant_type)
{
    std::vector<Prototype> prototypes;
    for (const auto& raw : library.prototypes) {
        auto prepared = growth::prepare_branch_module_prototype(raw, plant_type);
        if (!prepared) {
            continue; // skip non-branch objects (e.g. leaf quads without valid segments)
        }
        auto mature = growth::fully_grown_age(*prepared, plant_type);
        prototypes.push_back({.prepared = std::move(*prepared),
                              .seed_lambda = 0.0F,
                              .seed_determinacy = 0.0F,
                              .mature_age = mature ? *mature : 0.0F});
    }
    if (prototypes.empty()) {
        return std::unexpected(PlantError{PlantError::Code::EmptyLibrary, "no valid branch module prototypes"});
    }
    for (std::size_t i = 0; i < prototypes.size() && i < 9; ++i) {
        prototypes[i].seed_lambda = kMorphospaceLevels[i / 3];
        prototypes[i].seed_determinacy = kMorphospaceLevels[i % 3];
    }
    return prototypes;
}

[[nodiscard]] SimModule make_root(const std::vector<Prototype>& prototypes, const PlantTypeParameters& plant_type)
{
    // Root has no parent; select as if at full vigor (D' = D). (ADR-0013 / model doc.)
    const float lambda = current_apical_control(plant_type, 0.0F);
    const float determinacy = current_determinacy(plant_type, 0.0F);
    const std::size_t prototype_index = select_prototype(lambda, determinacy, prototypes.size());
    SimModule root;
    root.prototype_index = prototype_index;
    root.parent = kNoParent;
    root.frame = Frame{};
    root.terminal_occupied.assign(prototypes[prototype_index].prepared.terminal_nodes.size(), false);
    return root;
}

void step_geometry(const std::vector<SimModule>& modules, const std::vector<Prototype>& prototypes,
                   const PlantTypeParameters& plant_type, std::vector<Sphere>& spheres)
{
    spheres.assign(modules.size(), Sphere{});
    for (std::size_t index = 0; index < modules.size(); ++index) {
        const auto points =
            module_world_points(prototypes[modules[index].prototype_index], plant_type, modules[index].frame,
                                modules[index].age);
        const auto bounds = bounding_sphere(points);
        if (bounds.valid) {
            spheres[index] = bounds.sphere;
        } else {
            spheres[index] = {.center = modules[index].frame.origin, .radius = growth::kEpsilon};
        }
    }
}

void shed_low_vigor(std::vector<SimModule>& modules, float threshold)
{
    if (modules.empty()) {
        return;
    }
    std::vector<bool> keep(modules.size(), true);
    // A module is shed if its own vigor is below threshold (root is never shed) or an
    // ancestor was shed. Parents precede children, so a single forward pass suffices.
    for (std::size_t index = 1; index < modules.size(); ++index) {
        const std::size_t parent = modules[index].parent;
        if (!keep[parent] || modules[index].vigor < threshold) {
            keep[index] = false;
        }
    }
    std::vector<std::size_t> remap(modules.size(), kNoParent);
    std::vector<SimModule> kept;
    kept.reserve(modules.size());
    for (std::size_t index = 0; index < modules.size(); ++index) {
        if (!keep[index]) {
            continue;
        }
        remap[index] = kept.size();
        SimModule module = modules[index];
        module.parent = module.parent == kNoParent ? kNoParent : remap[module.parent];
        module.children.clear();
        kept.push_back(std::move(module));
    }
    modules = std::move(kept);
}

// Rebuild geometry, self-collision light (SS eq1), and Borchert-Honda vigor for the whole
// architecture at the given plant clock. Used each step and once more after the loop so the
// returned modules (including last-step attachments) carry vigor consistent with the state.
void refresh_vigor(std::vector<SimModule>& modules, const std::vector<Prototype>& prototypes,
                   const PlantTypeParameters& plant_type, std::vector<Sphere>& spheres, float plant_clock)
{
    rebuild_children(modules);
    step_geometry(modules, prototypes, plant_type, spheres);
    for (std::size_t index = 0; index < modules.size(); ++index) {
        modules[index].light = std::exp(-collision_measure(index, spheres[index], modules, spheres));
    }
    accumulate_light(modules);
    const float lambda = current_apical_control(plant_type, plant_clock);
    const float root_vigor = plant_type.root_max_vigor * senescence_factor(plant_type, plant_clock);
    distribute_vigor(modules, prototypes, lambda, root_vigor);
}

// Recompute per-module terminal occupancy from the actual children, so terminals freed
// by shedding become available again.
void recompute_occupancy(std::vector<SimModule>& modules, const std::vector<Prototype>& prototypes)
{
    for (SimModule& module : modules) {
        module.terminal_occupied.assign(prototypes[module.prototype_index].prepared.terminal_nodes.size(), false);
    }
    for (const SimModule& module : modules) {
        if (module.parent == kNoParent) {
            continue;
        }
        const auto& parent_terminals = prototypes[modules[module.parent].prototype_index].prepared.terminal_nodes;
        for (std::size_t ordinal = 0; ordinal < parent_terminals.size(); ++ordinal) {
            if (parent_terminals[ordinal] == module.parent_terminal_node) {
                modules[module.parent].terminal_occupied[ordinal] = true;
                break;
            }
        }
    }
}

void attach_modules(std::vector<SimModule>& modules, const std::vector<Prototype>& prototypes,
                    const PlantTypeParameters& plant_type, const std::vector<Sphere>& spheres, float plant_age,
                    float threshold)
{
    recompute_occupancy(modules, prototypes);

    const float lambda = current_apical_control(plant_type, plant_age);
    const float determinacy = current_determinacy(plant_type, plant_age);
    const std::size_t existing = modules.size();
    // Accumulate new modules separately: pushing into `modules` mid-loop would reallocate
    // and invalidate references, and children must not collide-test against their siblings.
    std::vector<SimModule> new_modules;
    for (std::size_t index = 0; index < existing; ++index) {
        const Prototype& prototype = prototypes[modules[index].prototype_index];
        if (modules[index].age < prototype.mature_age || prototype.prepared.terminal_nodes.empty()) {
            continue;
        }
        // Equal split of module vigor across terminal nodes (intra-module light q(n_i) =
        // Q(u)/#n is uniform). NOTE(decision): approximates the intra-module BH split.
        const float terminal_vigor =
            modules[index].vigor / static_cast<float>(prototype.prepared.terminal_nodes.size());
        if (terminal_vigor <= threshold) {
            continue;
        }
        const float determinacy_prime = plant_type.root_max_vigor <= growth::kEpsilon
                                            ? determinacy
                                            : modules[index].vigor * determinacy / plant_type.root_max_vigor;
        const Vec3 root_local = prototype.prepared.nodes[prototype.prepared.root_node].position;
        for (std::size_t terminal = 0; terminal < prototype.prepared.terminal_nodes.size(); ++terminal) {
            if (modules[index].terminal_occupied[terminal]) {
                continue;
            }
            const std::size_t terminal_node = prototype.prepared.terminal_nodes[terminal];
            const std::size_t child_prototype = select_prototype(lambda, determinacy_prime, prototypes.size());

            SimModule child;
            child.prototype_index = child_prototype;
            child.parent = index;
            child.parent_terminal_node = terminal_node;
            child.terminal_occupied.assign(prototypes[child_prototype].prepared.terminal_nodes.size(), false);

            Frame frame = modules[index].frame; // A.1: start from parent orientation.
            frame.origin = to_world(modules[index].frame, prototype.prepared.nodes[terminal_node].position, root_local);
            child.frame = optimize_orientation(prototypes[child_prototype], plant_type, frame, spheres, modules, index);
            modules[index].terminal_occupied[terminal] = true;
            new_modules.push_back(std::move(child));
        }
    }
    for (SimModule& child : new_modules) {
        modules.push_back(std::move(child));
    }
}

} // namespace

Result<PlantArchitecture> develop_plant(const PlantTypeParameters& plant_type,
                                        const import::BranchModulePrototypeLibrary& library, float plant_age)
{
    if (!std::isfinite(plant_age) || plant_age < 0.0F) {
        return std::unexpected(invalid_input("plant age must be finite and non-negative"));
    }
    if (!growth::plant_type_parameters_are_valid(plant_type)) {
        return std::unexpected(invalid_input("plant type parameters are invalid"));
    }
    auto prototypes = prepare_prototypes(library, plant_type);
    if (!prototypes) {
        return std::unexpected(prototypes.error());
    }

    const float threshold = shed_threshold(plant_type);
    std::vector<SimModule> modules;
    modules.push_back(make_root(*prototypes, plant_type));

    const float root_mature = (*prototypes)[modules.front().prototype_index].mature_age;
    const float lifespan = std::max(growth::kEpsilon, plant_type.plant_growth_rate * plant_type.plant_max_age);
    const float age_scale = kLifespanMaturities * std::max(root_mature, growth::kEpsilon) / lifespan;

    std::vector<Sphere> spheres;
    float plant_clock = 0.0F;
    float remaining = plant_age;
    int guard = 0;

    // Fixed step of kSimStep, with a final partial step so the clock lands exactly on
    // plant_age (no rounding overshoot; develop_plant(0.6) integrates to 0.6, not 1.0).
    while (remaining > growth::kEpsilon && guard < kMaxSimSteps) {
        const float dt = std::min(kSimStep, remaining);

        refresh_vigor(modules, *prototypes, plant_type, spheres, plant_clock);
        shed_low_vigor(modules, threshold);
        rebuild_children(modules);

        for (SimModule& module : modules) {
            const growth::VigorInputs vigor{
                .vigor = module.vigor, .min_vigor = threshold, .max_vigor = plant_type.root_max_vigor};
            const auto rate = growth::growth_rate(plant_type, vigor);
            if (rate) {
                module.age += *rate * dt * age_scale; // Paper: da_u/dt = Upsilon(u)
            }
        }

        step_geometry(modules, *prototypes, plant_type, spheres);
        attach_modules(modules, *prototypes, plant_type, spheres, plant_clock, threshold);

        plant_clock += dt;
        remaining -= dt;
        ++guard;
    }

    // Final consistency pass: assign vigor for the final clock (so last-step attachments are
    // not stale-zero) and shed sub-threshold modules, so every returned module satisfies the
    // shedding rule and senescence is fully resolved at the returned plant age.
    refresh_vigor(modules, *prototypes, plant_type, spheres, plant_clock);
    shed_low_vigor(modules, threshold);
    rebuild_children(modules);

    PlantArchitecture architecture;
    architecture.plant_age = plant_clock;
    architecture.senescent = plant_clock >= plant_type.plant_max_age;
    architecture.modules.reserve(modules.size());
    for (const SimModule& module : modules) {
        const Prototype& prototype = (*prototypes)[module.prototype_index];
        auto snapshot = growth::make_growth_snapshot(prototype.prepared, plant_type, module.age);
        PlacedModule placed;
        placed.prototype_index = module.prototype_index;
        placed.parent_module = module.parent;
        placed.parent_terminal_node = module.parent_terminal_node;
        placed.origin = module.frame.origin;
        placed.basis_x = module.frame.x;
        placed.basis_y = module.frame.y;
        placed.basis_z = module.frame.z;
        placed.physiological_age = module.age;
        placed.vigor = module.vigor;
        if (snapshot) {
            placed.snapshot = std::move(*snapshot);
        }
        architecture.modules.push_back(std::move(placed));
    }
    return architecture;
}

PlantArchitectureSummary summarize(const PlantArchitecture& architecture)
{
    PlantArchitectureSummary summary;
    summary.module_count = architecture.modules.size();
    summary.senescent = architecture.senescent;
    for (const PlacedModule& module : architecture.modules) {
        summary.visible_segment_count += module.snapshot.segments.size();
        for (const auto& segment : module.snapshot.segments) {
            summary.max_diameter = std::max(summary.max_diameter, segment.diameter);
        }
        if (module.parent_module == kNoParent) {
            summary.root_vigor = module.vigor;
        }
    }
    return summary;
}

} // namespace toi::plant
