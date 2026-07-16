#include "toi/growth/growth.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <queue>
#include <utility>

namespace toi::growth {
namespace {

constexpr Vec3 kTropismAxis{0.0F, 0.0F, 1.0F};
constexpr float kTropismRemainingAtFullLength = 0.25F;
constexpr float kCentimetersToMeters = 0.01F;
constexpr float kMainAxisEquivalenceRadians = 10.0F * std::numbers::pi_v<float> / 180.0F;

[[nodiscard]] GrowthError invalid_input(std::string message)
{
    return {GrowthError::Code::InvalidInput, std::move(message)};
}

[[nodiscard]] GrowthError invalid_prototype(std::string message)
{
    return {GrowthError::Code::InvalidPrototype, std::move(message)};
}

[[nodiscard]] bool finite_non_negative(float value)
{
    return std::isfinite(value) && value >= 0.0F;
}

[[nodiscard]] float smoothstep(float x)
{
    // Paper: S(x) = 3x² - 2x³.
    const float x_squared = x * x;
    return 3.0F * x_squared - 2.0F * x_squared * x;
}

[[nodiscard]] float tropism_falloff_age(float segment_max_length, float length_growth_scale)
{
    const float segment_full_age = segment_max_length / length_growth_scale;
    return kTropismRemainingAtFullLength * segment_full_age / (1.0F - kTropismRemainingAtFullLength);
}

[[nodiscard]] Vec3 tropism_offset(float segment_age, float tropism_falloff_age_value, float tropism_strength)
{
    if (tropism_falloff_age_value <= kEpsilon) {
        return {};
    }

    // Paper: τ_offset = g_dir · g₂ · g₁ / (a_b + g₁).
    return scale(kTropismAxis,
                 tropism_strength * tropism_falloff_age_value / (segment_age + tropism_falloff_age_value));
}

[[nodiscard]] Vec3 bent_segment_direction(const BranchSegment& segment, float segment_age, float tropism_strength)
{
    return normalize(
        add(segment.direction, tropism_offset(segment_age, segment.tropism_falloff_age, tropism_strength)));
}

[[nodiscard]] float segment_diameter(const BranchSegment& segment, float segment_age, float terminal_thickness)
{
    const float target_diameter = segment.pipe_diameter_factor * terminal_thickness;
    const float diameter_maturity = segment.inverse_remaining_diameter_age <= 0.0F
                                        ? 1.0F
                                        : std::clamp(segment_age * segment.inverse_remaining_diameter_age, 0.0F, 1.0F);
    return terminal_thickness + (target_diameter - terminal_thickness) * diameter_maturity;
}

[[nodiscard]] float direction_dot(Vec3 left, Vec3 right)
{
    return left.x * right.x + left.y * right.y + left.z * right.z;
}

[[nodiscard]] float downstream_reach(const BranchModulePrototype& prototype, std::size_t segment_index,
                                     std::vector<float>& memo)
{
    if (memo[segment_index] >= 0.0F) {
        return memo[segment_index];
    }
    const auto& segment = prototype.segments[segment_index];
    float child_reach = 0.0F;
    for (const auto child : prototype.child_segments_by_node[segment.child_node]) {
        child_reach = std::max(child_reach, downstream_reach(prototype, child, memo));
    }
    memo[segment_index] = segment.max_length + child_reach;
    return memo[segment_index];
}

void precompute_main_axis_continuations(BranchModulePrototype& prototype)
{
    prototype.main_child_segment_by_node.assign(prototype.nodes.size(), std::nullopt);
    std::vector<float> reaches(prototype.segments.size(), -1.0F);
    for (std::size_t index = 0; index < prototype.segments.size(); ++index) {
        (void)downstream_reach(prototype, index, reaches);
    }

    for (std::size_t node = 0; node < prototype.nodes.size(); ++node) {
        const auto incoming = prototype.incoming_segment_by_node[node];
        const auto& children = prototype.child_segments_by_node[node];
        if (!incoming || children.empty()) {
            continue;
        }
        const Vec3 incoming_direction = normalize(prototype.segments[*incoming].direction);
        float straightest_angle = std::numbers::pi_v<float>;
        for (const auto child : children) {
            const float cosine = std::clamp(direction_dot(incoming_direction,
                                                          normalize(prototype.segments[child].direction)),
                                            -1.0F, 1.0F);
            straightest_angle = std::min(straightest_angle, std::acos(cosine));
        }

        std::optional<std::size_t> selected;
        for (const auto child : children) {
            const auto& candidate = prototype.segments[child];
            const float candidate_dot = std::clamp(
                direction_dot(incoming_direction, normalize(candidate.direction)), -1.0F, 1.0F);
            if (std::acos(candidate_dot) > straightest_angle + kMainAxisEquivalenceRadians) {
                continue;
            }
            if (!selected) {
                selected = child;
                continue;
            }
            const auto& current = prototype.segments[*selected];
            const float current_dot = std::clamp(
                direction_dot(incoming_direction, normalize(current.direction)), -1.0F, 1.0F);
            const bool better = candidate.pipe_diameter_factor > current.pipe_diameter_factor + kEpsilon ||
                (std::abs(candidate.pipe_diameter_factor - current.pipe_diameter_factor) <= kEpsilon &&
                 (candidate_dot > current_dot + kEpsilon ||
                  (std::abs(candidate_dot - current_dot) <= kEpsilon &&
                   (reaches[child] > reaches[*selected] + kEpsilon ||
                    (std::abs(reaches[child] - reaches[*selected]) <= kEpsilon && child < *selected)))));
            if (better) {
                selected = child;
            }
        }
        // Paper: the selected child continues the main axis at this fork.
        prototype.main_child_segment_by_node[node] = selected;
    }

    std::size_t main_axis_node = prototype.root_node;
    while (!prototype.child_segments_by_node[main_axis_node].empty()) {
        const auto& children = prototype.child_segments_by_node[main_axis_node];
        const std::size_t continuation = children.size() == 1
            ? children.front()
            : *prototype.main_child_segment_by_node[main_axis_node];
        main_axis_node = prototype.segments[continuation].child_node;
    }
    prototype.main_axis_terminal_node = main_axis_node;
}

} // namespace

Vec3 add(Vec3 left, Vec3 right)
{
    return {left.x + right.x, left.y + right.y, left.z + right.z};
}

Vec3 subtract(Vec3 left, Vec3 right)
{
    return {left.x - right.x, left.y - right.y, left.z - right.z};
}

Vec3 scale(Vec3 value, float factor)
{
    return {value.x * factor, value.y * factor, value.z * factor};
}

float length(Vec3 value)
{
    return std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
}

float distance(Vec3 left, Vec3 right)
{
    return length(subtract(left, right));
}

Vec3 normalize(Vec3 value)
{
    const float value_length = length(value);
    if (value_length <= kEpsilon) {
        return {};
    }
    return scale(value, 1.0F / value_length);
}

std::span<const PlantTypeParameters> plant_type_presets()
{
    static constexpr std::array<PlantTypeParameters, 16> presets{{
        {20.0F, 42.0F, 0.19F, 0.62F, std::nullopt, 0.25F, std::nullopt, 0.0F, 0.52F, 0.63F, -0.38F, 0.57F, 0.47F},
        {200.0F, 78.0F, 0.30F, 0.84F, std::nullopt, 1.0F, std::nullopt, 0.0F, 0.52F, 0.63F, -1.2F, 1.0F, 0.79F},
        {80.0F, 11.0F, 0.80F, 1.0F, std::nullopt, 1.0F, std::nullopt, 0.0F, 0.9F, 0.5F, 1.0F, 5.0F, 1.95F},
        {16.0F, 1.7F, 0.23F, 0.44F, std::nullopt, 0.31F, std::nullopt, 0.0F, 1.0F, 1.0F, 1.0F, 5.0F, 3.0F},
        {430.0F, 600.0F, 0.15F, 1.0F, 0.5F, 1.0F, 0.33F, 58.0F, 0.52F, 0.63F, 0.56F, 3.0F, 1.23F},
        {550.0F, 450.0F, 0.20F, 0.76F, std::nullopt, 0.82F, std::nullopt, 0.0F, 0.17F, 0.5F, 0.47F, 1.20F, 1.90F},
        {550.0F, 700.0F, 0.20F, 0.9F, 0.5F, 0.93F, 0.74F, 55.0F, 0.17F, 0.5F, 0.47F, 1.38F, 0.94F},
        {500.0F, 570.0F, 0.24F, 1.0F, 0.5F, 1.0F, 0.5F, 55.0F, 0.5F, 0.27F, -0.66F, 1.38F, 1.29F},
        {950.0F, 900.0F, 0.12F, 0.87F, 0.34F, 0.93F, 0.55F, 57.0F, 0.66F, 0.14F, 0.2F, 1.41F, 1.29F},
        {950.0F, 600.0F, 0.14F, 1.0F, 0.5F, 1.0F, 0.51F, 57.0F, 0.45F, 0.63F, -0.9F, 0.82F, 0.93F},
        {950.0F, 600.0F, 0.14F, 1.0F, 0.5F, 1.0F, 0.51F, 57.0F, -0.2F, 0.63F, -0.9F, 0.82F, 0.93F},
        {1000.0F, 815.0F, 0.19F, 0.92F, 0.7F, 0.59F, 0.56F, 80.0F, -0.19F, 0.72F, -0.21F, 5.0F, 1.54F},
        {130.0F, 400.0F, 0.21F, 0.88F, 0.43F, 0.9F, 0.7F, 66.0F, 0.85F, 0.55F, 0.9F, 1.42F, 1.11F},
        {52.0F, 200.0F, 0.55F, 0.96F, 0.43F, 0.48F, 0.7F, 0.0F, -0.27F, 0.43F, 0.73F, 1.50F, 2.50F},
        {300.0F, 600.0F, 0.20F, 0.8F, std::nullopt, 0.86F, std::nullopt, 0.0F, -0.19F, 0.81F, 1.0F, 1.0F, 1.60F},
        {450.0F, 450.0F, 0.15F, 1.0F, 0.5F, 0.66F, 0.33F, 135.0F, 0.52F, 0.32F, 0.42F, 1.50F, 1.06F},
    }};
    return presets;
}

std::optional<PlantTypeParameters> plant_type_preset_by_key(char preset_key)
{
    const auto presets = plant_type_presets();
    if (preset_key < 'a' || preset_key > 'p') {
        return std::nullopt;
    }
    return presets[static_cast<std::size_t>(preset_key - 'a')];
}

bool plant_type_parameters_are_valid(const PlantTypeParameters& parameters)
{
    return finite_non_negative(parameters.plant_max_age) && finite_non_negative(parameters.root_max_vigor) &&
           finite_non_negative(parameters.plant_growth_rate) && finite_non_negative(parameters.apical_control) &&
           (!parameters.mature_apical_control || finite_non_negative(*parameters.mature_apical_control)) &&
           finite_non_negative(parameters.determinacy) &&
           (!parameters.mature_determinacy || finite_non_negative(*parameters.mature_determinacy)) &&
           finite_non_negative(parameters.flowering_age) && std::isfinite(parameters.tropism_angle) &&
           finite_non_negative(parameters.tropism_weight) && std::isfinite(parameters.tropism_strength) &&
           finite_non_negative(parameters.terminal_thickness) && std::isfinite(parameters.length_growth_scale) &&
           parameters.length_growth_scale > 0.0F;
}

VigorInputs VigorInputs::max_for(const PlantTypeParameters& plant_type)
{
    return {.vigor = plant_type.root_max_vigor, .min_vigor = 0.0F, .max_vigor = plant_type.root_max_vigor};
}

Result<float> growth_rate(const PlantTypeParameters& plant_type, const VigorInputs& vigor)
{
    if (!std::isfinite(vigor.vigor) || !std::isfinite(vigor.min_vigor) || !std::isfinite(vigor.max_vigor) ||
        vigor.max_vigor <= vigor.min_vigor) {
        return std::unexpected(invalid_input("vigor inputs must be finite with max_vigor > min_vigor"));
    }

    const float clamped_vigor = std::clamp(vigor.vigor, vigor.min_vigor, vigor.max_vigor);
    // Paper: (v̄(u) - v̄_min) / (v̄_max - v̄_min), normalized vigor.
    const float normalized_vigor = (clamped_vigor - vigor.min_vigor) / (vigor.max_vigor - vigor.min_vigor);
    const float smoothed_vigor = smoothstep(normalized_vigor);

    // Paper: Υ(u), growth rate of module u.
    return smoothed_vigor * plant_type.plant_growth_rate;
}

Result<BranchModulePrototype> prepare_branch_module_prototype(const BranchModulePrototype& prototype,
                                                              const PlantTypeParameters& plant_type)
{
    if (!plant_type_parameters_are_valid(plant_type)) {
        return std::unexpected(invalid_input("plant type parameters are invalid"));
    }

    auto prepared = prototype;
    const auto required_prototype = require_valid_branch_module_prototype(prepared);
    if (!required_prototype) {
        return std::unexpected(required_prototype.error());
    }

    std::vector<float> node_ages(prepared.nodes.size(), 0.0F);
    // Paper: β is authored in centimeters per physiological-age unit.
    const float length_growth_scale = plant_type.length_growth_scale * kCentimetersToMeters;
    const float inverse_length_growth_scale = 1.0F / length_growth_scale;
    std::queue<std::size_t> queue;
    queue.push(prepared.root_node);

    while (!queue.empty()) {
        const std::size_t node = queue.front();
        queue.pop();

        for (const std::size_t segment_index : prepared.child_segments_by_node[node]) {
            const auto& segment = prepared.segments[segment_index];
            // Paper: a_n, provisional from scaled root-to-node path length and Eq. 9 β.
            node_ages[segment.child_node] = node_ages[node] + segment.max_length * inverse_length_growth_scale;
            queue.push(segment.child_node);
        }
    }

    float max_physiological_age = 0.0F;
    for (std::size_t node_index = 0; node_index < prepared.nodes.size(); ++node_index) {
        prepared.nodes[node_index].physiological_age = node_ages[node_index];
        max_physiological_age = std::max(max_physiological_age, node_ages[node_index]);
    }
    prepared.max_physiological_age = max_physiological_age;

    for (auto& segment : prepared.segments) {
        const float base_node_age = prepared.nodes[segment.parent_node].physiological_age;
        const float remaining_age = max_physiological_age - base_node_age;
        segment.inverse_remaining_diameter_age = remaining_age <= kEpsilon ? 0.0F : 1.0F / remaining_age;
        segment.tropism_falloff_age = tropism_falloff_age(segment.max_length, length_growth_scale);
    }
    precompute_main_axis_continuations(prepared);

    return prepared;
}

Result<GrowthSnapshot> make_growth_snapshot(const BranchModulePrototype& prepared_prototype,
                                            const PlantTypeParameters& plant_type, float module_physiological_age)
{
    if (!std::isfinite(module_physiological_age) || module_physiological_age < 0.0F) {
        return std::unexpected(invalid_input("module physiological age must be finite and non-negative"));
    }
    if (!plant_type_parameters_are_valid(plant_type)) {
        return std::unexpected(invalid_input("plant type parameters are invalid"));
    }
    const auto required_prototype = require_valid_branch_module_prototype(prepared_prototype);
    if (!required_prototype) {
        return std::unexpected(required_prototype.error());
    }

    // Paper: φ and β are authored in centimeters; snapshots use meter-based plant space.
    const float terminal_thickness = plant_type.terminal_thickness * kCentimetersToMeters;
    const float length_growth_scale = plant_type.length_growth_scale * kCentimetersToMeters;

    std::vector<Vec3> current_positions(prepared_prototype.nodes.size());
    current_positions[prepared_prototype.root_node] = prepared_prototype.nodes[prepared_prototype.root_node].position;

    auto rate = growth_rate(plant_type, VigorInputs::max_for(plant_type));
    if (!rate) {
        return std::unexpected(rate.error());
    }

    GrowthSnapshot snapshot{
        .module_physiological_age = module_physiological_age,
        .growth_rate = *rate,
        .segments = {},
    };
    snapshot.segments.reserve(prepared_prototype.segments.size());

    for (std::size_t segment_index = 0; segment_index < prepared_prototype.segments.size(); ++segment_index) {
        const auto& segment = prepared_prototype.segments[segment_index];
        const Vec3 parent_position = current_positions[segment.parent_node];
        // Paper: a_n, physiological age of the segment base node.
        const float base_node_age = prepared_prototype.nodes[segment.parent_node].physiological_age;
        // Paper: a_b = max(0, a_u - a_n), segment physiological age.
        const float segment_age = std::max(0.0F, module_physiological_age - base_node_age);
        // Paper: ℓ_b = min(ℓ_max, β · a_b), current segment length.
        const float segment_length = std::min(segment.max_length, length_growth_scale * segment_age);

        const Vec3 segment_direction = segment_length <= kEpsilon
                                           ? segment.direction
                                           : bent_segment_direction(segment, segment_age, plant_type.tropism_strength);
        const Vec3 child_position = add(parent_position, scale(segment_direction, segment_length));
        current_positions[segment.child_node] = child_position;

        if (segment_length <= kEpsilon) {
            continue;
        }

        const SegmentState state =
            segment_length + kEpsilon < segment.max_length ? SegmentState::Growing : SegmentState::Mature;
        snapshot.segments.push_back({
            .source_segment_id = segment_index,
            .parent_position = parent_position,
            .child_position = child_position,
            .diameter = segment_diameter(segment, segment_age, terminal_thickness),
            .state = state,
        });
    }

    return snapshot;
}

Result<float> fully_grown_age(const BranchModulePrototype& prepared_prototype, const PlantTypeParameters& plant_type)
{
    if (!plant_type_parameters_are_valid(plant_type)) {
        return std::unexpected(invalid_input("plant type parameters are invalid"));
    }
    const auto required_prototype = require_valid_branch_module_prototype(prepared_prototype);
    if (!required_prototype) {
        return std::unexpected(required_prototype.error());
    }

    // Paper: β is authored in centimeters per physiological-age unit.
    const float length_growth_scale = plant_type.length_growth_scale * kCentimetersToMeters;
    float result = 0.0F;
    for (const auto& segment : prepared_prototype.segments) {
        const float base_node_age = prepared_prototype.nodes[segment.parent_node].physiological_age;
        result = std::max(result, base_node_age + segment.max_length / length_growth_scale);
    }
    return result;
}

std::vector<float> compute_pipe_diameter_factors(const std::vector<BranchSegment>& segments,
                                                 const std::vector<std::vector<std::size_t>>& child_segments_by_node)
{
    std::vector<float> factors(segments.size(), 0.0F);

    for (std::size_t reverse_index = segments.size(); reverse_index > 0; --reverse_index) {
        const std::size_t segment_index = reverse_index - 1;
        const auto& segment = segments[segment_index];
        float child_sum_squares = 0.0F;
        for (const std::size_t child_segment_index : child_segments_by_node[segment.child_node]) {
            const float factor = factors[child_segment_index];
            child_sum_squares += factor * factor;
        }

        // Paper: d_b = sqrt(sum(d_c²)); terminal factor is 1 because d_b = φ.
        factors[segment_index] = child_sum_squares > 0.0F ? std::sqrt(child_sum_squares) : 1.0F;
    }

    return factors;
}

Result<void> require_valid_branch_module_prototype(const BranchModulePrototype& prototype)
{
    if (prototype.nodes.empty() || prototype.segments.empty()) {
        return std::unexpected(invalid_prototype("prototype topology is empty"));
    }
    if (prototype.root_node >= prototype.nodes.size()) {
        return std::unexpected(invalid_prototype("prototype root node is out of bounds"));
    }
    if (prototype.child_segments_by_node.size() != prototype.nodes.size() ||
        prototype.incoming_segment_by_node.size() != prototype.nodes.size()) {
        return std::unexpected(invalid_prototype("prototype node indexes are incomplete"));
    }
    if (prototype.incoming_segment_by_node[prototype.root_node]) {
        return std::unexpected(invalid_prototype("prototype root cannot have an incoming segment"));
    }
    if (!prototype.main_child_segment_by_node.empty() &&
        prototype.main_child_segment_by_node.size() != prototype.nodes.size()) {
        return std::unexpected(invalid_prototype("prototype main-child index is incomplete"));
    }

    for (const auto& node : prototype.nodes) {
        if (!std::isfinite(node.position.x) || !std::isfinite(node.position.y) || !std::isfinite(node.position.z) ||
            !std::isfinite(node.physiological_age)) {
            return std::unexpected(invalid_prototype("prototype branch node has invalid values"));
        }
    }
    for (std::size_t segment_index = 0; segment_index < prototype.segments.size(); ++segment_index) {
        const auto& segment = prototype.segments[segment_index];
        if (segment.parent_node >= prototype.nodes.size() || segment.child_node >= prototype.nodes.size() ||
            segment.parent_node == segment.child_node) {
            return std::unexpected(invalid_prototype("branch segment references invalid nodes"));
        }
        if (!std::isfinite(segment.direction.x) || !std::isfinite(segment.direction.y) ||
            !std::isfinite(segment.direction.z) || length(segment.direction) <= kEpsilon ||
            !std::isfinite(segment.max_length) || segment.max_length <= kEpsilon ||
            !std::isfinite(segment.pipe_diameter_factor) || !std::isfinite(segment.inverse_remaining_diameter_age) ||
            !std::isfinite(segment.tropism_falloff_age)) {
            return std::unexpected(invalid_prototype("branch segment has invalid values"));
        }
    }

    std::vector<bool> reached_nodes(prototype.nodes.size(), false);
    std::vector<bool> reached_segments(prototype.segments.size(), false);
    std::queue<std::size_t> queue;
    queue.push(prototype.root_node);
    reached_nodes[prototype.root_node] = true;
    while (!queue.empty()) {
        const auto node = queue.front();
        queue.pop();
        for (const auto segment_index : prototype.child_segments_by_node[node]) {
            if (segment_index >= prototype.segments.size() || reached_segments[segment_index]) {
                return std::unexpected(invalid_prototype("prototype child segment index is invalid"));
            }
            const auto& segment = prototype.segments[segment_index];
            if (segment.parent_node != node || reached_nodes[segment.child_node]) {
                return std::unexpected(invalid_prototype("prototype is not a rooted tree"));
            }
            if (prototype.incoming_segment_by_node[segment.child_node] != segment_index) {
                return std::unexpected(invalid_prototype("prototype incoming and child indexes disagree"));
            }
            reached_segments[segment_index] = true;
            reached_nodes[segment.child_node] = true;
            queue.push(segment.child_node);
        }
        if (!prototype.main_child_segment_by_node.empty()) {
            const auto main = prototype.main_child_segment_by_node[node];
            if (main && std::ranges::find(prototype.child_segments_by_node[node], *main) ==
                            prototype.child_segments_by_node[node].end()) {
                return std::unexpected(invalid_prototype("prototype main child is not a child of its node"));
            }
        }
    }
    if (std::ranges::find(reached_nodes, false) != reached_nodes.end() ||
        std::ranges::find(reached_segments, false) != reached_segments.end()) {
        return std::unexpected(invalid_prototype("prototype topology is disconnected"));
    }
    for (std::size_t node = 0; node < prototype.nodes.size(); ++node) {
        if (node != prototype.root_node && !prototype.incoming_segment_by_node[node]) {
            return std::unexpected(invalid_prototype("non-root node has no incoming segment"));
        }
    }

    if (prototype.terminal_nodes.empty()) {
        return std::unexpected(invalid_prototype("prototype has no terminal nodes"));
    }
    std::vector<bool> terminals(prototype.nodes.size(), false);
    for (const auto terminal : prototype.terminal_nodes) {
        if (terminal >= prototype.nodes.size() || terminals[terminal] ||
            !prototype.child_segments_by_node[terminal].empty()) {
            return std::unexpected(invalid_prototype("prototype terminal nodes are invalid"));
        }
        terminals[terminal] = true;
    }
    for (std::size_t node = 0; node < prototype.nodes.size(); ++node) {
        if (prototype.child_segments_by_node[node].empty() && !terminals[node]) {
            return std::unexpected(invalid_prototype("prototype leaf is not a terminal node"));
        }
    }
    return {};
}

} // namespace toi::growth
