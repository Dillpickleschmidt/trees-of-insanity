#pragma once

#include "toi/growth/growth.hpp"

#include <cstddef>
#include <optional>
#include <span>
#include <vector>

namespace toi::growth::detail {

struct ConduitModuleView {
    const BranchModulePrototype* prototype = nullptr;
    std::optional<std::size_t> parent_module_index;
    std::optional<std::size_t> parent_terminal_node;
    float physiological_age = 0.0F;
    std::span<float> developed_diameters;
};

struct ConduitSegmentState {
    float diameter = 0.0F;
    std::optional<std::size_t> main_continuation_segment;
};

class PlantConduit {
public:
    // Rebuilds derived topology and retains each segment's greatest developed diameter.
    [[nodiscard]] static Result<PlantConduit> develop(std::span<ConduitModuleView> modules,
                                                       float terminal_thickness);

    [[nodiscard]] std::span<const ConduitSegmentState> module_segments(std::size_t module_index) const;
    [[nodiscard]] std::optional<std::size_t> attached_child(std::size_t module_index,
                                                            std::size_t local_node) const;

private:
    PlantConduit() = default;

    std::vector<std::size_t> module_node_offsets_;
    std::vector<std::size_t> module_segment_offsets_;
    std::vector<std::optional<std::size_t>> child_module_by_local_node_;
    std::vector<ConduitSegmentState> segment_states_;
};

} // namespace toi::growth::detail
