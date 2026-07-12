#include "toi/import/obj_importer.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <fstream>
#include <iterator>
#include <queue>
#include <set>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace toi::import {
namespace {

using growth::BranchModulePrototype;
using growth::BranchNode;
using growth::BranchSegment;
using growth::Vec3;

struct ObjObjectData {
    std::string name;
    std::vector<std::size_t> vertex_ids;
    std::vector<std::pair<std::size_t, std::size_t>> edges;
};

struct ParsedObj {
    std::vector<Vec3> global_vertices;
    std::vector<ObjObjectData> objects;
};

[[nodiscard]] ImportError make_error(ImportError::Code code, std::string message)
{
    return {.code = code, .message = std::move(message)};
}

[[nodiscard]] ImportError parse_error(std::size_t line_number, std::string_view message)
{
    return make_error(ImportError::Code::Parse,
                      "OBJ parse error on line " + std::to_string(line_number) + ": " + std::string(message));
}

[[nodiscard]] Result<std::string> read_text_file(const std::filesystem::path& path)
{
    std::ifstream file(path);
    if (!file) {
        return std::unexpected(make_error(ImportError::Code::Io, "failed to read " + path.string()));
    }

    std::ostringstream out;
    out << file.rdbuf();
    return out.str();
}

[[nodiscard]] std::string_view trim(std::string_view value)
{
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) {
        value.remove_prefix(1);
    }
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) {
        value.remove_suffix(1);
    }
    return value;
}

[[nodiscard]] std::vector<std::string_view> split_whitespace(std::string_view value)
{
    std::vector<std::string_view> tokens;
    while (!value.empty()) {
        value = trim(value);
        if (value.empty()) {
            break;
        }
        std::size_t token_end = 0;
        while (token_end < value.size() && !std::isspace(static_cast<unsigned char>(value[token_end]))) {
            ++token_end;
        }
        tokens.push_back(value.substr(0, token_end));
        value.remove_prefix(token_end);
    }
    return tokens;
}

[[nodiscard]] Result<float> parse_float(std::string_view value, std::size_t line_number)
{
    float result = 0.0F;
    const auto* begin = value.data();
    const auto* end = value.data() + value.size();
    const auto [ptr, err] = std::from_chars(begin, end, result);
    if (err != std::errc{} || ptr != end) {
        return std::unexpected(parse_error(line_number, "invalid float " + std::string(value)));
    }
    return result;
}

[[nodiscard]] Result<std::size_t> parse_obj_vertex_index(std::string_view token, std::size_t line_number)
{
    const std::size_t slash = token.find('/');
    const std::string_view index_text = slash == std::string_view::npos ? token : token.substr(0, slash);

    int parsed = 0;
    const auto* begin = index_text.data();
    const auto* end = index_text.data() + index_text.size();
    const auto [ptr, err] = std::from_chars(begin, end, parsed);
    if (err != std::errc{} || ptr != end) {
        return std::unexpected(parse_error(line_number, "invalid vertex index " + std::string(token)));
    }
    if (parsed <= 0) {
        return std::unexpected(parse_error(line_number, "only positive OBJ vertex indices are supported"));
    }
    return static_cast<std::size_t>(parsed);
}

[[nodiscard]] Result<Vec3> parse_vertex(std::string_view rest, std::size_t line_number)
{
    const auto values = split_whitespace(rest);
    if (values.size() < 3) {
        return std::unexpected(parse_error(line_number, "vertex needs at least 3 coordinates"));
    }

    auto x = parse_float(values[0], line_number);
    if (!x) {
        return std::unexpected(x.error());
    }
    auto y = parse_float(values[1], line_number);
    if (!y) {
        return std::unexpected(y.error());
    }
    auto z = parse_float(values[2], line_number);
    if (!z) {
        return std::unexpected(z.error());
    }

    return Vec3{.x = *x, .y = *y, .z = *z};
}

[[nodiscard]] Result<std::vector<std::size_t>> parse_line_indices(std::string_view rest, std::size_t line_number)
{
    const auto tokens = split_whitespace(rest);
    if (tokens.size() < 2) {
        return std::unexpected(parse_error(line_number, "line needs at least 2 vertex indices"));
    }

    std::vector<std::size_t> indices;
    indices.reserve(tokens.size());
    for (const auto token : tokens) {
        auto parsed = parse_obj_vertex_index(token, line_number);
        if (!parsed) {
            return std::unexpected(parsed.error());
        }
        indices.push_back(*parsed);
    }
    return indices;
}

[[nodiscard]] Result<ParsedObj> parse_obj_text(std::string_view text)
{
    ParsedObj parsed;
    ObjObjectData* active_object = nullptr;

    std::size_t line_number = 0;
    while (!text.empty()) {
        ++line_number;
        const std::size_t newline = text.find('\n');
        std::string_view raw_line = newline == std::string_view::npos ? text : text.substr(0, newline);
        if (!raw_line.empty() && raw_line.back() == '\r') {
            raw_line.remove_suffix(1);
        }
        text = newline == std::string_view::npos ? std::string_view{} : text.substr(newline + 1);

        const std::string_view line = trim(raw_line);
        if (line.empty() || line.starts_with('#')) {
            continue;
        }

        if (line.starts_with("o ")) {
            parsed.objects.push_back({.name = std::string(trim(line.substr(2))), .vertex_ids = {}, .edges = {}});
            active_object = &parsed.objects.back();
            continue;
        }

        if (line.starts_with("v ")) {
            auto vertex = parse_vertex(line.substr(2), line_number);
            if (!vertex) {
                return std::unexpected(vertex.error());
            }
            parsed.global_vertices.push_back(*vertex);
            if (active_object != nullptr) {
                active_object->vertex_ids.push_back(parsed.global_vertices.size());
            }
            continue;
        }

        if (line.starts_with("l ") && active_object != nullptr) {
            auto indices = parse_line_indices(line.substr(2), line_number);
            if (!indices) {
                return std::unexpected(indices.error());
            }
            for (std::size_t index = 1; index < indices->size(); ++index) {
                active_object->edges.emplace_back((*indices)[index - 1], (*indices)[index]);
            }
        }
    }

    return parsed;
}

[[nodiscard]] Result<ParsedObj> parse_obj_file(const std::filesystem::path& path)
{
    auto text = read_text_file(path);
    if (!text) {
        return std::unexpected(text.error());
    }
    return parse_obj_text(*text);
}

[[nodiscard]] Vec3 obj_to_app_coordinates(Vec3 obj_position)
{
    return {.x = obj_position.x, .y = obj_position.z, .z = obj_position.y};
}

[[nodiscard]] Result<Vec3> vertex_by_obj_id(const std::vector<Vec3>& global_vertices, std::size_t obj_id)
{
    if (obj_id == 0 || obj_id > global_vertices.size()) {
        return std::unexpected(make_error(ImportError::Code::InvalidPrototype,
                                          "vertex index " + std::to_string(obj_id) + " is out of bounds"));
    }
    return global_vertices[obj_id - 1];
}

[[nodiscard]] Result<BranchModulePrototype>
build_prototype(const ObjObjectData& object, const std::vector<Vec3>& global_vertices, std::size_t prototype_id,
                float prototype_geometry_scale)
{
    if (object.vertex_ids.empty()) {
        return std::unexpected(
            make_error(ImportError::Code::InvalidPrototype, "object " + object.name + " has no vertices"));
    }
    if (object.edges.empty()) {
        return std::unexpected(
            make_error(ImportError::Code::InvalidPrototype, "object " + object.name + " has no line segments"));
    }

    const std::size_t root_global_id = object.vertex_ids.front();
    auto root = vertex_by_obj_id(global_vertices, root_global_id);
    if (!root) {
        return std::unexpected(root.error());
    }
    const Vec3 root_position = obj_to_app_coordinates(*root);

    std::unordered_map<std::size_t, std::size_t> global_to_local;
    global_to_local.reserve(object.vertex_ids.size());
    for (std::size_t local = 0; local < object.vertex_ids.size(); ++local) {
        global_to_local.emplace(object.vertex_ids[local], local);
    }

    std::vector<BranchNode> nodes;
    nodes.reserve(object.vertex_ids.size());
    for (const std::size_t global_id : object.vertex_ids) {
        auto raw_position = vertex_by_obj_id(global_vertices, global_id);
        if (!raw_position) {
            return std::unexpected(raw_position.error());
        }
        const Vec3 app_position = obj_to_app_coordinates(*raw_position);
        nodes.push_back({
            .position = growth::scale(growth::subtract(app_position, root_position), prototype_geometry_scale),
            .physiological_age = 0.0F,
        });
    }

    // Some bundled OBJ objects contain duplicate coincident vertices joined by a
    // zero-length line. Collapse coincident vertices in the import adapter so the
    // growth model still receives a connected acyclic branch module prototype.
    std::vector<std::size_t> canonical_by_local(nodes.size(), 0);
    std::vector<BranchNode> canonical_nodes;
    canonical_nodes.reserve(nodes.size());
    for (std::size_t local = 0; local < nodes.size(); ++local) {
        const auto found = std::ranges::find_if(canonical_nodes, [&node = nodes[local]](const BranchNode& candidate) {
            return growth::distance(candidate.position, node.position) <= growth::kEpsilon;
        });
        if (found == canonical_nodes.end()) {
            canonical_by_local[local] = canonical_nodes.size();
            canonical_nodes.push_back(nodes[local]);
        } else {
            canonical_by_local[local] = static_cast<std::size_t>(std::distance(canonical_nodes.begin(), found));
        }
    }
    nodes = std::move(canonical_nodes);

    std::vector<std::pair<std::size_t, std::size_t>> undirected_edges;
    undirected_edges.reserve(object.edges.size());
    std::set<std::pair<std::size_t, std::size_t>> seen_edges;

    for (const auto& [a_global, b_global] : object.edges) {
        const auto a_found = global_to_local.find(a_global);
        const auto b_found = global_to_local.find(b_global);
        if (a_found == global_to_local.end() || b_found == global_to_local.end()) {
            return std::unexpected(make_error(ImportError::Code::InvalidPrototype,
                                              "line references vertex outside object " + object.name));
        }
        const std::size_t a = canonical_by_local[a_found->second];
        const std::size_t b = canonical_by_local[b_found->second];
        if (a == b) {
            continue;
        }
        const auto key = std::minmax(a, b);
        if (seen_edges.insert(key).second) {
            undirected_edges.emplace_back(a, b);
        }
    }

    if (undirected_edges.size() != nodes.size() - 1) {
        return std::unexpected(make_error(ImportError::Code::InvalidPrototype,
                                          "object " + object.name + " must be a tree: got " +
                                              std::to_string(nodes.size()) + " nodes and " +
                                              std::to_string(undirected_edges.size()) + " unique edges"));
    }

    std::vector<std::vector<std::pair<std::size_t, float>>> adjacency(nodes.size());
    for (const auto& [a, b] : undirected_edges) {
        const float edge_length = growth::distance(nodes[a].position, nodes[b].position);
        if (edge_length <= growth::kEpsilon) {
            return std::unexpected(
                make_error(ImportError::Code::InvalidPrototype, "line segment length must be positive"));
        }
        adjacency[a].emplace_back(b, edge_length);
        adjacency[b].emplace_back(a, edge_length);
    }

    BranchModulePrototype prototype;
    prototype.id = prototype_id;
    prototype.name = object.name;
    prototype.nodes = std::move(nodes);
    prototype.root_node = 0;
    prototype.child_segments_by_node.resize(prototype.nodes.size());
    prototype.incoming_segment_by_node.resize(prototype.nodes.size());

    std::vector<std::optional<std::size_t>> parent(prototype.nodes.size());
    std::queue<std::size_t> queue;
    parent[prototype.root_node] = prototype.root_node;
    queue.push(prototype.root_node);

    while (!queue.empty()) {
        const std::size_t node = queue.front();
        queue.pop();

        for (const auto& [neighbor, edge_length] : adjacency[node]) {
            if (parent[neighbor].has_value()) {
                continue;
            }
            parent[neighbor] = node;
            const std::size_t segment_index = prototype.segments.size();
            prototype.segments.push_back({
                .parent_node = node,
                .child_node = neighbor,
                .direction = growth::normalize(
                    growth::subtract(prototype.nodes[neighbor].position, prototype.nodes[node].position)),
                .pipe_diameter_factor = 0.0F,
                .inverse_remaining_diameter_age = 0.0F,
                .tropism_falloff_age = 0.0F,
                .max_length = edge_length,
            });
            prototype.child_segments_by_node[node].push_back(segment_index);
            prototype.incoming_segment_by_node[neighbor] = segment_index;
            queue.push(neighbor);
        }
    }

    if (std::ranges::any_of(parent, [](const auto& value) { return !value.has_value(); })) {
        return std::unexpected(
            make_error(ImportError::Code::InvalidPrototype, "object " + object.name + " is not connected"));
    }

    for (std::size_t node = 0; node < adjacency.size(); ++node) {
        if (node != prototype.root_node && adjacency[node].size() == 1) {
            prototype.terminal_nodes.push_back(node);
        }
    }

    const auto factors = growth::compute_pipe_diameter_factors(prototype.segments, prototype.child_segments_by_node);
    for (std::size_t index = 0; index < prototype.segments.size(); ++index) {
        prototype.segments[index].pipe_diameter_factor = factors[index];
    }

    const auto required_prototype = growth::require_valid_branch_module_prototype(prototype);
    if (!required_prototype) {
        return std::unexpected(make_error(ImportError::Code::InvalidPrototype, required_prototype.error().message));
    }

    return prototype;
}

[[nodiscard]] std::vector<ObjObjectData> sorted_objects_with_lines(ParsedObj parsed)
{
    std::erase_if(parsed.objects, [](const ObjObjectData& object) { return object.edges.empty(); });
    std::ranges::sort(parsed.objects, {}, &ObjObjectData::name);
    return std::move(parsed.objects);
}

} // namespace

Result<BranchModulePrototype> load_branch_module_prototype_from_obj(const std::filesystem::path& path,
                                                                    std::string_view object_name,
                                                                    std::size_t prototype_id,
                                                                    float prototype_geometry_scale)
{
    if (!std::isfinite(prototype_geometry_scale) || prototype_geometry_scale <= 0.0F) {
        return std::unexpected(make_error(ImportError::Code::InvalidInput,
                                          "prototype geometry scale must be finite and positive"));
    }

    auto parsed = parse_obj_file(path);
    if (!parsed) {
        return std::unexpected(parsed.error());
    }

    auto objects = sorted_objects_with_lines(*parsed);
    const auto found = std::ranges::find_if(
        objects, [object_name](const ObjObjectData& object) { return object.name == object_name; });
    if (found == objects.end()) {
        return std::unexpected(
            make_error(ImportError::Code::UnknownObject, "OBJ object not found: " + std::string(object_name)));
    }

    return build_prototype(*found, parsed->global_vertices, prototype_id, prototype_geometry_scale);
}

Result<BranchModulePrototypeLibrary> load_branch_module_prototype_library_from_obj(
    const std::filesystem::path& path, float prototype_geometry_scale)
{
    if (!std::isfinite(prototype_geometry_scale) || prototype_geometry_scale <= 0.0F) {
        return std::unexpected(make_error(ImportError::Code::InvalidInput,
                                          "prototype geometry scale must be finite and positive"));
    }

    auto parsed = parse_obj_file(path);
    if (!parsed) {
        return std::unexpected(parsed.error());
    }

    auto objects = sorted_objects_with_lines(*parsed);
    BranchModulePrototypeLibrary library;
    library.prototypes.reserve(objects.size());
    for (std::size_t index = 0; index < objects.size(); ++index) {
        auto prototype =
            build_prototype(objects[index], parsed->global_vertices, index, prototype_geometry_scale);
        if (!prototype) {
            return std::unexpected(prototype.error());
        }
        library.prototypes.push_back(std::move(*prototype));
    }
    return library;
}

std::optional<std::size_t> prototype_id_by_name(const BranchModulePrototypeLibrary& library, std::string_view name)
{
    const auto found = std::ranges::find_if(
        library.prototypes, [name](const BranchModulePrototype& prototype) { return prototype.name == name; });
    if (found == library.prototypes.end()) {
        return std::nullopt;
    }
    return found->id;
}

} // namespace toi::import
