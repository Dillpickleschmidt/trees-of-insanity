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
        UnknownPlantType,
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

inline constexpr std::size_t kNoParent = static_cast<std::size_t>(-1);

struct PlantError {
    enum class Code {
        InvalidInput,
        InvalidPrototype,
        EmptyLibrary,
    };

    Code code = Code::InvalidInput;
    std::string message;
};

template <class T> using PlantResult = std::expected<T, PlantError>;

struct PlacedModule {
    std::size_t prototype_index = 0;
    std::size_t parent_module = kNoParent;
    std::size_t parent_terminal_node = 0;
    Vec3 origin;
    Vec3 basis_x{1.0F, 0.0F, 0.0F};
    Vec3 basis_y{0.0F, 1.0F, 0.0F};
    Vec3 basis_z{0.0F, 0.0F, 1.0F};
    // Paper: a_u, module physiological age.
    float physiological_age = 0.0F;
    // Paper: v̄(u), module vigor.
    float vigor = 0.0F;
    GrowthSnapshot snapshot;
};

struct PlantArchitecture {
    std::vector<PlacedModule> modules;
    std::vector<BranchModulePrototype> prototypes;
    // Paper: p_t, plant physiological age.
    float plant_age = 0.0F;
    bool senescent = false;
};

struct PlantArchitectureSummary {
    std::size_t module_count = 0;
    std::size_t visible_segment_count = 0;
    float max_diameter = 0.0F;
    float root_vigor = 0.0F;
    bool senescent = false;
};

[[nodiscard]] PlantResult<PlantArchitecture>
develop_plant(const PlantTypeParameters& plant_type, const BranchModulePrototypeLibrary& library, float plant_age);

[[nodiscard]] PlantArchitectureSummary summarize(const PlantArchitecture& architecture);

} // namespace toi::growth
