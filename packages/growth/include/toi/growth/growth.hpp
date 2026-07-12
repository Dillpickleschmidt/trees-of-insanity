#pragma once

#include <cstddef>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace toi::growth {

constexpr float kEpsilon = 1.0e-6F;

struct Vec3 {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
};

[[nodiscard]] Vec3 add(Vec3 left, Vec3 right);
[[nodiscard]] Vec3 subtract(Vec3 left, Vec3 right);
[[nodiscard]] Vec3 scale(Vec3 value, float factor);
[[nodiscard]] float length(Vec3 value);
[[nodiscard]] float distance(Vec3 left, Vec3 right);
[[nodiscard]] Vec3 normalize(Vec3 value);

struct GrowthError {
    enum class Code {
        InvalidInput,
        InvalidPrototype,
    };

    Code code = Code::InvalidInput;
    std::string message;
};

template <class T> using Result = std::expected<T, GrowthError>;

struct PlantTypeParameters {
    float plant_max_age = 0.0F;
    float root_max_vigor = 0.0F;
    float plant_growth_rate = 0.0F;
    float apical_control = 0.0F;
    std::optional<float> mature_apical_control;
    float determinacy = 0.0F;
    std::optional<float> mature_determinacy;
    float flowering_age = 0.0F;
    float tropism_angle = 0.0F;
    float tropism_weight = 0.0F;
    float tropism_strength = 0.0F;
    float terminal_thickness = 0.0F;
    float length_growth_scale = 0.0F;
};

struct PlantTypeParameterDescriptor {
    std::string_view key;
    std::optional<float> min;
    std::optional<float> max;
};

[[nodiscard]] std::span<const PlantTypeParameters> plant_type_presets();
[[nodiscard]] std::optional<PlantTypeParameters> plant_type_preset_by_key(char preset_key);
[[nodiscard]] std::span<const PlantTypeParameterDescriptor> plant_type_parameter_descriptors();
[[nodiscard]] bool plant_type_parameters_are_valid(const PlantTypeParameters& parameters);

struct VigorInputs {
    float vigor = 0.0F;
    float min_vigor = 0.0F;
    float max_vigor = 0.0F;

    [[nodiscard]] static VigorInputs max_for(const PlantTypeParameters& plant_type);
};

struct BranchNode {
    Vec3 position;
    // Paper: a_n, node physiological age.
    float physiological_age = 0.0F;
};

struct BranchSegment {
    std::size_t parent_node = 0;
    std::size_t child_node = 0;
    Vec3 direction;
    // Paper: d_b / φ, final Eq. 8 diameter factor.
    float pipe_diameter_factor = 0.0F;
    float inverse_remaining_diameter_age = 0.0F;
    float tropism_falloff_age = 0.0F;
    // Paper: ℓ_max, maximum length this branch segment can attain.
    float max_length = 0.0F;
};

struct BranchModulePrototype {
    std::size_t id = 0;
    std::string name;
    std::vector<BranchNode> nodes;
    std::vector<BranchSegment> segments;
    std::size_t root_node = 0;
    std::vector<std::size_t> terminal_nodes;
    float max_physiological_age = 0.0F;
    std::vector<std::vector<std::size_t>> child_segments_by_node;
    std::vector<std::optional<std::size_t>> incoming_segment_by_node;
};

struct BranchModulePrototypeLibrary {
    std::vector<BranchModulePrototype> prototypes;
};

enum class SegmentState {
    Growing,
    Mature,
};

struct GrowthSnapshotSegment {
    std::size_t source_segment_id = 0;
    Vec3 parent_position;
    Vec3 child_position;
    float diameter = 0.0F;
    SegmentState state = SegmentState::Growing;
};

struct GrowthSnapshot {
    float module_physiological_age = 0.0F;
    float growth_rate = 0.0F;
    std::vector<GrowthSnapshotSegment> segments;
};

[[nodiscard]] Result<float> growth_rate(const PlantTypeParameters& plant_type, const VigorInputs& vigor);

[[nodiscard]] Result<BranchModulePrototype> prepare_branch_module_prototype(const BranchModulePrototype& prototype,
                                                                            const PlantTypeParameters& plant_type);

[[nodiscard]] Result<GrowthSnapshot> make_growth_snapshot(const BranchModulePrototype& prepared_prototype,
                                                          const PlantTypeParameters& plant_type,
                                                          float module_physiological_age);

[[nodiscard]] Result<float> fully_grown_age(const BranchModulePrototype& prepared_prototype,
                                            const PlantTypeParameters& plant_type);

[[nodiscard]] std::vector<float>
compute_pipe_diameter_factors(const std::vector<BranchSegment>& segments,
                              const std::vector<std::vector<std::size_t>>& child_segments_by_node);

[[nodiscard]] Result<void> require_valid_branch_module_prototype(const BranchModulePrototype& prototype);

[[nodiscard]] std::string to_string(SegmentState state);

struct Sphere {
    Vec3 center;
    float radius = 0.0F;
};

struct VigorSplit {
    float main_axis = 0.0F;
    float lateral_axis = 0.0F;
};

// Paper: f_collisions(u), raw intersection volume in cubic meters.
[[nodiscard]] float collision_measure(const Sphere& own_sphere, std::span<const Sphere> other_spheres);
// Paper: Q(u), module light exposure.
[[nodiscard]] float light_exposure(float collision_measure);
// Paper Eq. 2: binary Borchert-Honda vigor split.
[[nodiscard]] VigorSplit split_vigor(float available_vigor, float main_axis_light, float lateral_axis_light,
                                     float apical_control);
// Paper: D', vigor-scaled determinacy.
[[nodiscard]] float vigor_scaled_determinacy(float determinacy, float module_vigor, float root_max_vigor);
// Fixed 3x3 morphospace, row-major by apical control then determinacy.
[[nodiscard]] std::size_t nearest_morphospace_prototype(float apical_control, float determinacy);
[[nodiscard]] float parameter_for_plant_age(float young_value, std::optional<float> mature_value,
                                            float flowering_age, float plant_age);
// Paper Eq. 4 and Eq. 3 respectively.
[[nodiscard]] float orientation_tropism_cost(float tropism_angle, Vec3 module_axis, Vec3 tropism_axis);
[[nodiscard]] float orientation_distribution_cost(float collision_exposure, float collision_weight,
                                                  float tropism_cost, float tropism_weight);
// Paper Eq. 6: forward-Euler physiological-age integration.
[[nodiscard]] float physiological_age_euler_step(float physiological_age, float growth_rate, float time_step);

inline constexpr float kShedVigorFraction = 0.02F;
[[nodiscard]] float shedding_vigor_threshold(float root_max_vigor);
[[nodiscard]] float senescence_interpolation(float full_vigor, float plant_age, float maximum_age,
                                             float ramp_duration);

} // namespace toi::growth
