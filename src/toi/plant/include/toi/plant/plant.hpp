#pragma once

#include "toi/growth/growth.hpp"
#include "toi/import/obj_importer.hpp"

#include <cstddef>
#include <expected>
#include <string>
#include <vector>

namespace toi::plant {

using growth::Vec3;

// Sentinel parent index for the root branch module.
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

template <class T> using Result = std::expected<T, PlantError>;

// A branch module instance placed in a plant's module architecture. The module's
// geometry is the module-scale GrowthSnapshot in the module's local frame; origin +
// basis place that frame in the plant's world space.
struct PlacedModule {
    std::size_t prototype_index = 0;         // index into the prepared prototype set
    std::size_t parent_module = kNoParent;   // index into PlantArchitecture::modules
    std::size_t parent_terminal_node = 0;    // parent node this module attaches to

    Vec3 origin;                             // world position of the module root node
    Vec3 basis_x{1.0F, 0.0F, 0.0F};          // module-local +x in world space
    Vec3 basis_y{0.0F, 1.0F, 0.0F};          // module-local +y in world space
    Vec3 basis_z{0.0F, 0.0F, 1.0F};          // module-local +z in world space

    float physiological_age = 0.0F;          // Paper: a_u, module physiological age
    float vigor = 0.0F;                       // Paper: v̄(u), module vigor
    growth::GrowthSnapshot snapshot;          // module-local geometry at a_u
};

// A plant modeled as an ordered tree of connected branch modules (modules[0] is the
// root module). Deterministic for a given (plant type, prototype library, plant age).
struct PlantArchitecture {
    std::vector<PlacedModule> modules;
    float plant_age = 0.0F;                  // Paper: p_t, plant physiological age
    bool senescent = false;                  // p_t >= p_max reached
};

struct PlantArchitectureSummary {
    std::size_t module_count = 0;
    std::size_t visible_segment_count = 0;
    float max_diameter = 0.0F;
    float root_vigor = 0.0F;
    bool senescent = false;
};

// Develop a plant to the given plant age by integrating the plant-scale growth model
// (SS 5.2) from age 0: self-collision light, Borchert-Honda vigor, module growth,
// attachment/shedding, senescence, morphospace selection, and module orientation.
[[nodiscard]] Result<PlantArchitecture>
develop_plant(const growth::PlantTypeParameters& plant_type,
              const import::BranchModulePrototypeLibrary& library, float plant_age);

[[nodiscard]] PlantArchitectureSummary summarize(const PlantArchitecture& architecture);

} // namespace toi::plant
