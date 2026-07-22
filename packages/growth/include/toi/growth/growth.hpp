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
        ResourceExhausted,
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

[[nodiscard]] std::span<const PlantTypeParameters> plant_type_presets();
[[nodiscard]] std::optional<PlantTypeParameters> plant_type_preset_by_key(char preset_key);
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
    std::vector<std::optional<std::size_t>> main_child_segment_by_node;
    std::size_t main_axis_terminal_node = 0;
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

struct Sphere {
    Vec3 center;
    float radius = 0.0F;
};

inline constexpr float kMinimumModuleVigor = 0.02F;
inline constexpr float kMaximumModuleVigor = 1.0F;

struct SnapshotRange {
    std::size_t offset = 0;
    std::size_t count = 0;
};

struct RigidTransform {
    Vec3 x_axis{1.0F, 0.0F, 0.0F};
    Vec3 y_axis{0.0F, 1.0F, 0.0F};
    Vec3 z_axis{0.0F, 0.0F, 1.0F};
    Vec3 translation;
};

[[nodiscard]] Vec3 transform_direction(const RigidTransform& transform, Vec3 direction);
[[nodiscard]] Vec3 transform_point(const RigidTransform& transform, Vec3 point);

struct PlantSegmentSnapshot {
    std::size_t module_id = 0;
    std::size_t source_segment_id = 0;
    Vec3 parent_position;
    Vec3 child_position;
    Vec3 mature_parent_position;
    Vec3 mature_child_position;
    float diameter = 0.0F;
    SegmentState state = SegmentState::Growing;
    std::optional<std::size_t> main_continuation_segment;
};

struct PlantModuleSnapshot {
    std::size_t id = 0;
    std::size_t prototype_id = 0;
    std::optional<std::size_t> parent_module_id;
    std::optional<std::size_t> parent_terminal_node;
    RigidTransform transform;
    Vec3 root_position;
    float physiological_age = 0.0F;
    float fully_grown_age = 0.0F;
    float direct_light_exposure = 0.0F;
    float accumulated_light = 0.0F;
    float vigor = 0.0F;
    float growth_rate = 0.0F;
    Sphere collision_sphere;
    SnapshotRange segments;
};

enum class TerminalAxisRole {
    Main,
    Lateral,
};

struct MatureTerminalSnapshot {
    std::size_t module_id = 0;
    std::size_t terminal_node = 0;
    Vec3 position;
    Vec3 tangent;
    float host_radius = 0.0F;
    float vigor = 0.0F;
    TerminalAxisRole axis_role = TerminalAxisRole::Lateral;
    std::optional<std::size_t> child_module_id;
};

struct AttachmentEvent {
    std::size_t child_module_id = 0;
    std::size_t parent_module_id = 0;
    std::size_t parent_terminal_node = 0;
    std::size_t prototype_id = 0;
};

struct PlantSnapshot {
    float plant_age = 0.0F;
    std::span<const PlantModuleSnapshot> modules;
    std::span<const PlantSegmentSnapshot> segments;
    std::span<const MatureTerminalSnapshot> mature_terminals;
    std::span<const AttachmentEvent> attachment_events;
};

class PlantSimulation {
public:
    [[nodiscard]] static Result<PlantSimulation> create(const BranchModulePrototypeLibrary& prototype_library,
                                                        const PlantTypeParameters& plant_type,
                                                        std::size_t root_prototype_id);

    [[nodiscard]] Result<void> step(float timestep);

    // Views remain valid until the next successful step or simulation destruction.
    [[nodiscard]] PlantSnapshot snapshot() const;

private:
    struct PrototypeOrientationData {
        Sphere mature_sphere;
        std::vector<std::size_t> ordered_terminal_nodes;
    };

    struct ModuleRecord {
        std::size_t id = 0;
        std::size_t prototype_index = 0;
        std::optional<std::size_t> parent_module_index;
        std::optional<std::size_t> parent_terminal_node;
        RigidTransform transform;
        float physiological_age = 0.0F;
        float fully_grown_age = 0.0F;
        std::vector<float> developed_diameters;
        bool diagnostics_active = true;
    };

    PlantSimulation() = default;

    [[nodiscard]] Result<void> attach_crossed_modules(
        std::span<const std::size_t> crossed_parent_indices,
        std::span<const float> preintegration_vigor,
        std::vector<Sphere> orientation_spheres);
    [[nodiscard]] Result<void> rebuild_snapshot();

    PlantTypeParameters plant_type_;
    std::vector<BranchModulePrototype> prepared_prototypes_;
    std::vector<PrototypeOrientationData> prototype_orientation_data_;
    std::vector<ModuleRecord> module_records_;
    std::size_t next_module_id_ = 1;
    float plant_age_ = 0.0F;
    std::vector<PlantModuleSnapshot> snapshot_modules_;
    std::vector<PlantSegmentSnapshot> snapshot_segments_;
    std::vector<MatureTerminalSnapshot> snapshot_terminals_;
    std::vector<AttachmentEvent> snapshot_attachment_events_;
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
[[nodiscard]] float vigor_scaled_determinacy(float determinacy, float module_vigor);
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

} // namespace toi::growth
