// Sketch drawing tools (#49, docs/adr/0015-sketch-tools.md): state machines that turn pointer
// events on the sketch plane into sketch entities. The shells only convert screen positions into
// sketch coordinates, so every platform draws the same way.
#pragma once

#include "facetry/core/mesh.hpp"
#include "facetry/core/session.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace facetry::core {

enum class SketchToolKind : std::uint8_t {
    Line = 0,                 // a polyline, point by point; dragging back from its last point draws
                              // a tangent arc
    RectangleTwoPoint = 1,    // opposite corners; the sides follow the sketch axes
    RectangleThreePoint = 2,  // one side, then the width
    RectangleCenter = 3,      // the center, then a corner
    CircleCenter = 4,         // the center, then a point on the circle
    CircleTwoPoint = 5,       // the ends of a diameter
    CircleThreePoint = 6,     // three points on the circle
    ArcThreePoint = 7,        // start, end, then a point on the arc
    ArcCenter = 8,            // the center, the start, then the end; the way round follows the
                              // pointer
    ArcTangent = 9,           // from the end of a line or arc, tangent to it, to the end
    Point = 10,
};

enum class PointerEvent : std::uint8_t { Down = 0, Move = 1, Up = 2 };

// Edges of a tool's preview from this ID on are guides (alignment and inference lines); draw them
// dashed.
inline constexpr std::uint32_t kPreviewGuideId = 1001;

// What the pointer snapped to and what the tool infers there, for showing next to the pointer.
struct SnapFeedback {
    // "point", "center", "midpoint", "quadrant", "intersection", "origin", "onCurve", "grid", or
    // empty when the pointer is free.
    std::string snap;
    // Constraints the next click would add: "horizontal", "vertical", "parallel",
    // "perpendicular", "tangent".
    std::vector<std::string> inferences;
};

class SketchTool {
  public:
    // A tool for the sketch feature `sketch`. New curves are construction curves if
    // `construction`.
    SketchTool(SketchToolKind kind, std::string sketch, bool construction);

    // A pointer event at (u, v) on the sketch plane, in millimeters along the sketch's axes. A
    // click is Down then Up at about the same place; a drag from one place to another counts as
    // two clicks. Positions within `snapRadius` millimeters of a point of the sketch use that
    // point. Returns true if the tool changed the document (it completed a shape); the session
    // needs a regeneration then. Throws Error(InvalidArgument) if the sketch does not exist.
    bool pointer(Session& session, PointerEvent event, double u, double v, double snapRadius);

    // Esc: drops the shape in progress, or ends the polyline. Returns false if nothing was in
    // progress; the shell then leaves the tool.
    bool cancel();

    // What the tool would draw next, for display like sketchMesh(): curves as edges (guides from
    // kPreviewGuideId on), the points fixed so far and the pointer as vertices. Empty when the
    // pointer has not moved yet.
    [[nodiscard]] Mesh preview(const Session& session, double deflection) const;

    // The snap and inferences at the pointer.
    [[nodiscard]] SnapFeedback feedback() const;

    // Grid snap: free positions round to multiples of `spacing` millimeters; 0 turns it off.
    void setGrid(double spacing) noexcept {
        grid_ = spacing > 0.0 ? spacing : 0.0;
    }

    [[nodiscard]] SketchToolKind kind() const noexcept {
        return kind_;
    }

    // An inferred constraint for the next line: its type and the other curve, if any.
    struct Inference {
        std::string type;
        std::string other;
    };

    // A position on the sketch plane in millimeters, what it snapped to, and what the line tool
    // infers there. A new point there gets the constraints of its snap: a midpoint, a point on a
    // curve (quadrants and on-curve snaps) or on two (intersections).
    struct Spot {
        double u = 0.0;
        double v = 0.0;
        std::string point{};                // an existing point it snapped to
        std::string kind{};                 // as in SnapFeedback::snap
        std::vector<std::string> curves{};  // the curves it lies on
        std::vector<Inference> inferences{};
        std::vector<std::pair<std::pair<double, double>, std::pair<double, double>>> guides{};
    };

  private:
    // The direction leaving the end of a curve, and the curve.
    struct Tangent {
        std::pair<double, double> direction;
        std::string curve;
        bool arc = false;
    };

    [[nodiscard]] Spot snap(const Session& session, double u, double v, double radius) const;
    bool click(Session& session, const Spot& spot, double snapRadius);
    [[nodiscard]] std::optional<Tangent> tangentAt(const Session& session, const Spot& end) const;
    // The points of the shape so far, with a drag in progress counting as a click.
    [[nodiscard]] std::vector<Spot> placed(double radius) const;

    SketchToolKind kind_;
    std::string sketch_;
    bool construction_;
    std::vector<Spot> fixed_;      // points clicked for the shape in progress
    std::optional<Spot> pointer_;  // where the pointer is
    std::optional<Spot> pressed_;  // where it went down, while it is down
    double lastRadius_ = 0.0;      // the snap radius of the last event, for the preview
    // Polylines: the direction at the last point, for tangent arcs, and the first point, which
    // ends the polyline when it is clicked again.
    std::optional<std::pair<double, double>> tangent_;
    std::string chainStart_;
    std::string lastCurve_;  // the polyline's last curve, for tangent and perpendicular
    bool lastCurveArc_ = false;
    double grid_ = 0.0;
    // Arcs by center: how far the pointer went round the center since the start was clicked.
    double sweep_ = 0.0;
    double lastAngle_ = 0.0;
};

}  // namespace facetry::core
