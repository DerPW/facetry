#pragma once

// Persistent naming (docs/adr/0006-persistent-naming.md): modeling operations that name every
// face, edge and vertex of their result from the operation's history, so that references
// survive regeneration.

#include "facetry/core/shape.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace facetry::core {

enum class ElementKind : std::uint8_t { Face = 0, Edge = 1, Vertex = 2 };

[[nodiscard]] std::string_view toString(ElementKind kind) noexcept;

// A shape plus a persistent name for each of its faces, edges and vertices. Element ID n (as in
// Shape) has the name at index n - 1.
struct NamedShape {
    Shape shape;
    std::vector<std::string> faces;
    std::vector<std::string> edges;
    std::vector<std::string> vertices;

    [[nodiscard]] const std::vector<std::string>& names(ElementKind kind) const;
    // The 1-based ID of the element with this name, if there is one.
    [[nodiscard]] std::optional<std::uint32_t> find(ElementKind kind, std::string_view name) const;
};

// Where an element is and what it looks like: the geometric fingerprint stored next to a
// reference's name, used to report and (with a warning) recover references whose name is gone.
struct ElementGeometry {
    std::string type;  // "plane", "cylinder", … for faces; "line", "circle", … for edges; "point"
    Vec3 centroid;
    double size = 0.0;  // area of a face, length of an edge, 0 for a vertex

    friend bool operator==(const ElementGeometry&, const ElementGeometry&) = default;
};

[[nodiscard]] ElementGeometry elementGeometry(const Shape& shape, ElementKind kind,
                                              std::uint32_t id);

// True if two fingerprints describe the same element within the modeling tolerance.
[[nodiscard]] bool sameGeometry(const ElementGeometry& a, const ElementGeometry& b) noexcept;

// A straight sketch edge in the XY plane, named by its sketch (for example "f1/l2").
struct ProfileSegment {
    std::string name;
    double x0 = 0.0;
    double y0 = 0.0;
    double x1 = 0.0;
    double y1 = 0.0;
};

// Extrudes a closed loop of segments (in order, each starting where the previous one ends) along
// +Z by `distance` (negative: along -Z). Names: side faces "<feature>:side[<segment>]", the cap
// in the sketch plane "<feature>:start", the other cap "<feature>:end".
[[nodiscard]] NamedShape extrudeProfile(std::span<const ProfileSegment> loop, double distance,
                                        std::string_view feature);

// Rounds edges (1-based IDs of `body`) with a constant radius. Faces keep their names, rounds are
// named "<feature>:fillet[<edge name>]".
[[nodiscard]] NamedShape filletEdges(const NamedShape& body, std::span<const std::uint32_t> edgeIds,
                                     double radius, std::string_view feature);

}  // namespace facetry::core
