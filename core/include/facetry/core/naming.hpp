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

// A sketch's plane in space: its origin and the unit directions of the sketch's x and y axes. The
// normal is x × y; extrusions go along it.
struct SketchFrame {
    Vec3 origin{0.0, 0.0, 0.0};
    Vec3 xAxis{1.0, 0.0, 0.0};
    Vec3 yAxis{0.0, 1.0, 0.0};

    [[nodiscard]] Vec3 normal() const noexcept;
    // The point at sketch coordinates (x, y).
    [[nodiscard]] Vec3 at(double x, double y) const noexcept;

    friend bool operator==(const SketchFrame&, const SketchFrame&) = default;
};

// The frames of the origin planes: "xy" (normal +Z), "xz" (x along X, y along Z, normal -Y) and
// "yz" (x along Y, y along Z, normal +X), like Fusion's.
[[nodiscard]] SketchFrame originPlaneFrame(std::string_view plane);

// A sketch curve in its plane, named by its sketch (for example "f1/l2" or "f1/a3").
struct ProfileSegment {
    enum class Kind : std::uint8_t {
        Line,
        Arc,    // around (cx, cy), clockwise if `clockwise`, from (x0, y0) to (x1, y1)
        Circle  // around (cx, cy) with `radius`; a loop on its own
    };

    std::string name;
    double x0 = 0.0;
    double y0 = 0.0;
    double x1 = 0.0;
    double y1 = 0.0;
    Kind kind = Kind::Line;
    double cx = 0.0;
    double cy = 0.0;
    double radius = 0.0;  // circles; an arc's radius is its start's distance from the center
    bool clockwise = false;
};

// Extrudes a closed loop of segments (in order, each starting where the previous one ends), or a
// single circle, along the frame's normal by `distance` (negative: the other way). Names: side
// faces "<feature>:side[<segment>]", the cap in the sketch plane "<feature>:start", the other cap
// "<feature>:end".
[[nodiscard]] NamedShape extrudeProfile(std::span<const ProfileSegment> loop, double distance,
                                        std::string_view feature, const SketchFrame& frame = {});

// A closed region of a sketch, a profile (ADR-0014): a face on the sketch plane, its normal along
// the sketch normal, bounded by pieces of the sketch's curves. Its name lists the curves around it
// and the side it lies on, sorted: "region{f1/c5-|f1/l1+|f1/l2+|f1/l3+|f1/l4+}". "+" is left of
// the curve's direction (lines from start to end, arcs and circles counter-clockwise), "-" right
// of it. Regions with the same name get "#1", "#2", … in geometric order.
struct SketchRegion {
    std::string name;
    Shape face;
    std::vector<std::string> edges;  // per edge ID of `face`: the curve it is a piece of
};

// The regions that the curves enclose, sorted by name. Curves are split where they cross or
// touch; islands are holes in the region around them and regions of their own. Curves that
// enclose nothing, such as an open line, bound no region. Degenerate curves are ignored.
[[nodiscard]] std::vector<SketchRegion> sketchRegions(std::span<const ProfileSegment> curves,
                                                      const SketchFrame& frame);

// The regions' faces as one shape for display and picking: face ID i + 1 is regions[i].
[[nodiscard]] Shape regionsShape(std::span<const SketchRegion> regions);

// Extrudes regions of one sketch (from one sketchRegions() call) along the frame's normal by
// `distance` into one body; adjacent regions merge. Names as extrudeProfile(): a side face is
// named after the curve its edge is a piece of; caps are "<feature>:start" and "<feature>:end".
[[nodiscard]] NamedShape extrudeRegions(std::span<const SketchRegion> regions,
                                        const SketchFrame& frame, double distance,
                                        std::string_view feature);

// The frame of a planar face (1-based ID of `body`), its normal pointing out of the body, for
// sketching on it. Throws Error(InvalidArgument) if the face is not planar.
[[nodiscard]] SketchFrame planarFaceFrame(const NamedShape& body, std::uint32_t faceId);

// An edge (1-based ID of `body`) as seen on the plane of `frame`, in its coordinates (#52): a
// line, or an arc (counter-clockwise) or circle when the edge is one in a plane parallel to the
// sketch's. Throws Error(InvalidArgument) for edges whose projection is a point or another kind of
// curve (an ellipse, a spline), which projection does not support yet.
[[nodiscard]] ProfileSegment projectEdge(const NamedShape& body, std::uint32_t edgeId,
                                         const SketchFrame& frame);

// The edges around a face (1-based IDs of `body`), sorted.
[[nodiscard]] std::vector<std::uint32_t> faceEdges(const NamedShape& body, std::uint32_t faceId);

// Rounds edges (1-based IDs of `body`) with a constant radius. Faces keep their names, rounds are
// named "<feature>:fillet[<edge name>]".
[[nodiscard]] NamedShape filletEdges(const NamedShape& body, std::span<const std::uint32_t> edgeIds,
                                     double radius, std::string_view feature);

}  // namespace facetry::core
