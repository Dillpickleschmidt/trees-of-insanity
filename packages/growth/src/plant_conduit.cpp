#include "plant_conduit.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>
#include <string>
#include <utility>

namespace toi::growth::detail {
namespace {

[[nodiscard]] GrowthError invalid_conduit(std::string message);
[[nodiscard]] float segment_diameter_maturity(const BranchModulePrototype& prototype,
                                               const BranchSegment& segment,
                                               float module_physiological_age);

} // namespace

Result<PlantConduit> PlantConduit::develop(std::span<ConduitModuleView> modules,
                                           float terminal_thickness)
{
    struct ConduitEdge {
        std::size_t module_index = 0;
        std::size_t source_segment_id = 0;
        std::size_t parent_node = 0;
        std::size_t child_node = 0;
    };

    PlantConduit conduit;
    const std::size_t module_count = modules.size();
    conduit.module_node_offsets_.assign(module_count + 1, 0);
    conduit.module_segment_offsets_.assign(module_count + 1, 0);
    for (std::size_t module = 0; module < module_count; ++module) {
        const auto* prototype = modules[module].prototype;
        if (prototype == nullptr || modules[module].developed_diameters.size() != prototype->segments.size()) {
            return std::unexpected(invalid_conduit("module pipe state does not match its prototype"));
        }
        conduit.module_node_offsets_[module + 1] =
            conduit.module_node_offsets_[module] + prototype->nodes.size();
        conduit.module_segment_offsets_[module + 1] =
            conduit.module_segment_offsets_[module] + prototype->segments.size();
    }

    const auto unassigned = std::numeric_limits<std::size_t>::max();
    std::vector<std::size_t> global_node_by_local_node(conduit.module_node_offsets_.back(), unassigned);
    conduit.child_module_by_local_node_.resize(conduit.module_node_offsets_.back());
    std::size_t node_count = 0;
    for (std::size_t module = 0; module < module_count; ++module) {
        const auto& record = modules[module];
        const auto& prototype = *record.prototype;
        if (record.parent_module_index) {
            if (*record.parent_module_index >= module || !record.parent_terminal_node) {
                return std::unexpected(invalid_conduit("module attachments must follow parent-first order"));
            }
            const auto parent_local_node = conduit.module_node_offsets_[*record.parent_module_index] +
                *record.parent_terminal_node;
            global_node_by_local_node[conduit.module_node_offsets_[module] + prototype.root_node] =
                global_node_by_local_node[parent_local_node];
            conduit.child_module_by_local_node_[parent_local_node] = module;
        }
        for (std::size_t node = 0; node < prototype.nodes.size(); ++node) {
            auto& global_node = global_node_by_local_node[conduit.module_node_offsets_[module] + node];
            if (global_node == unassigned) {
                global_node = node_count++;
            }
        }
    }

    std::vector<ConduitEdge> edges;
    edges.reserve(conduit.module_segment_offsets_.back());
    std::vector<std::size_t> child_edge_counts(node_count, 0);
    for (std::size_t module = 0; module < module_count; ++module) {
        const auto& prototype = *modules[module].prototype;
        for (std::size_t source = 0; source < prototype.segments.size(); ++source) {
            const auto& segment = prototype.segments[source];
            const auto parent_node = global_node_by_local_node[
                conduit.module_node_offsets_[module] + segment.parent_node];
            const auto child_node = global_node_by_local_node[
                conduit.module_node_offsets_[module] + segment.child_node];
            edges.push_back({
                .module_index = module,
                .source_segment_id = source,
                .parent_node = parent_node,
                .child_node = child_node,
            });
            ++child_edge_counts[parent_node];
        }
    }

    std::vector<std::size_t> child_edge_offsets(node_count + 1, 0);
    for (std::size_t node = 0; node < node_count; ++node) {
        child_edge_offsets[node + 1] = child_edge_offsets[node] + child_edge_counts[node];
    }
    std::vector<std::size_t> child_edges(edges.size());
    auto child_edge_cursors = child_edge_offsets;
    for (std::size_t edge = 0; edge < edges.size(); ++edge) {
        child_edges[child_edge_cursors[edges[edge].parent_node]++] = edge;
    }

    std::vector<std::size_t> preorder_edges;
    preorder_edges.reserve(edges.size());
    std::queue<std::size_t> nodes;
    if (!modules.empty()) {
        nodes.push(global_node_by_local_node[modules.front().prototype->root_node]);
    }
    while (!nodes.empty()) {
        const auto node = nodes.front();
        nodes.pop();
        for (std::size_t index = child_edge_offsets[node]; index < child_edge_offsets[node + 1]; ++index) {
            const auto edge = child_edges[index];
            preorder_edges.push_back(edge);
            nodes.push(edges[edge].child_node);
        }
    }
    if (preorder_edges.size() != edges.size()) {
        return std::unexpected(invalid_conduit("plant conduit must be one rooted tree"));
    }
    const std::vector<std::size_t> postorder_edges(preorder_edges.rbegin(), preorder_edges.rend());

    conduit.segment_states_.resize(edges.size());
    std::vector<float> current_diameters(edges.size(), terminal_thickness);
    for (const auto edge_index : postorder_edges) {
        const auto& edge = edges[edge_index];
        auto& module = modules[edge.module_index];
        const auto& prototype = *module.prototype;
        const auto& segment = prototype.segments[edge.source_segment_id];
        float child_area = 0.0F;
        for (std::size_t index = child_edge_offsets[edge.child_node];
             index < child_edge_offsets[edge.child_node + 1]; ++index) {
            const float child_diameter = current_diameters[child_edges[index]];
            child_area += child_diameter * child_diameter;
        }
        // Paper: d_b, supported segment diameter; φ, terminal thickness.
        const float support_diameter = child_area > 0.0F ? std::sqrt(child_area) : terminal_thickness;
        const float maturity = segment_diameter_maturity(prototype, segment, module.physiological_age);
        const float candidate = terminal_thickness + (support_diameter - terminal_thickness) * maturity;
        auto& developed = module.developed_diameters[edge.source_segment_id];
        developed = std::max(developed, candidate);
        current_diameters[edge_index] = developed;
    }

    for (std::size_t module = 0; module < module_count; ++module) {
        const auto& prototype = *modules[module].prototype;
        for (std::size_t source = 0; source < prototype.segments.size(); ++source) {
            const std::size_t segment_index = conduit.module_segment_offsets_[module] + source;
            const auto child_node = prototype.segments[source].child_node;
            std::optional<std::size_t> continuation;
            if (const auto main = prototype.main_child_segment_by_node[child_node]) {
                continuation = conduit.module_segment_offsets_[module] + *main;
            } else if (const auto child = conduit.attached_child(module, child_node)) {
                const auto& child_prototype = *modules[*child].prototype;
                continuation = conduit.module_segment_offsets_[*child] +
                    child_prototype.child_segments_by_node[child_prototype.root_node].front();
            }
            conduit.segment_states_[segment_index] = {
                .diameter = current_diameters[segment_index],
                .main_continuation_segment = continuation,
            };
        }
    }
    return conduit;
}

std::span<const ConduitSegmentState> PlantConduit::module_segments(std::size_t module_index) const
{
    const std::size_t offset = module_segment_offsets_[module_index];
    return std::span<const ConduitSegmentState>(segment_states_).subspan(
        offset, module_segment_offsets_[module_index + 1] - offset);
}

std::optional<std::size_t> PlantConduit::attached_child(std::size_t module_index,
                                                        std::size_t local_node) const
{
    return child_module_by_local_node_[module_node_offsets_[module_index] + local_node];
}

namespace {

GrowthError invalid_conduit(std::string message)
{
    return {
        .code = GrowthError::Code::InvalidPrototype,
        .message = std::move(message),
    };
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

} // namespace
} // namespace toi::growth::detail
