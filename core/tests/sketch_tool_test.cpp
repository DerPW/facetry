// Sketch drawing tools (core/include/facetry/core/sketch_tool.hpp).
#include "facetry/core/sketch_tool.hpp"

#include "facetry/core/error.hpp"
#include "facetry/core/session_json.hpp"

#include "corner_fillet.hpp"

#include <doctest.h>

#include <cmath>
#include <numbers>
#include <string>
#include <vector>

using namespace facetry::core;
using namespace facetry::test;

namespace {

constexpr double kSnap = 0.5;

// An empty sketch f1 on XY and an extrusion of it by 2.
Session emptySketch(LengthUnit units = LengthUnit::Millimeter) {
    Document document;
    document.units = units;
    document.root.timeline.push_back(feature("f1", "Sketch 1", Sketch{}));
    document.root.timeline.push_back(
        feature("f2", "Extrude 1", Extrude{.profile = "f1", .distance = 2.0, .regions = {}}));
    Session session(document);
    session.regenerate();
    return session;
}

const Sketch& sketchOf(const Session& session) {
    return std::get<Sketch>(session.document().root.timeline[0].data);
}

bool click(SketchTool& tool, Session& session, double u, double v) {
    tool.pointer(session, PointerEvent::Down, u, v, kSnap);
    return tool.pointer(session, PointerEvent::Up, u, v, kSnap);
}

bool drag(SketchTool& tool, Session& session, double u0, double v0, double u1, double v1) {
    tool.pointer(session, PointerEvent::Down, u0, v0, kSnap);
    tool.pointer(session, PointerEvent::Move, (u0 + u1) / 2, (v0 + v1) / 2, kSnap);
    return tool.pointer(session, PointerEvent::Up, u1, v1, kSnap);
}

void clicks(SketchTool& tool, Session& session, std::vector<std::pair<double, double>> spots) {
    for (const auto& [u, v] : spots) {
        click(tool, session, u, v);
    }
}

// The position of a sketch point, in document units.
std::pair<double, double> at(const Session& session, const std::string& point) {
    for (const SketchPoint& p : sketchOf(session).points) {
        if (p.id == point) {
            return {p.x.number, p.y.number};
        }
    }
    FAIL("no point " << point);
    return {};
}

double extrudedArea(Session& session) {
    const Regeneration& regeneration = session.regenerate();
    const FeatureResult& extrude = regeneration.features.at(1);
    INFO((extrude.messages.empty() ? std::string() : extrude.messages.front()));
    REQUIRE(extrude.status == FeatureStatus::Ok);
    return regeneration.model.bodies.at("f2")->shape.properties().volume / 2.0;
}

std::vector<std::string> constraintTypes(const Session& session) {
    std::vector<std::string> types;
    for (const SketchConstraint& constraint : sketchOf(session).constraints) {
        types.push_back(constraint.type);
    }
    return types;
}

}  // namespace

TEST_CASE("the line tool draws a polyline that closes on its first point") {
    Session session = emptySketch();
    SketchTool tool(SketchToolKind::Line, "f1", false);
    CHECK_FALSE(click(tool, session, 0, 0));  // the first point only starts the polyline
    CHECK(click(tool, session, 10, 0));
    clicks(tool, session, {{10, 10}, {0, 10}});
    CHECK(click(tool, session, 0.2, -0.1));  // snaps to the first point and closes
    const Sketch& sketch = sketchOf(session);
    CHECK(sketch.lines.size() == 4);
    CHECK(sketch.points.size() == 4);  // corners are shared
    CHECK(sketch.lines.back().end == sketch.lines.front().start);
    CHECK_FALSE(tool.cancel());  // nothing in progress after closing
    CHECK(extrudedArea(session) == doctest::Approx(100));

    // Clicking the same point twice draws nothing.
    SketchTool again(SketchToolKind::Line, "f1", false);
    click(again, session, 20, 0);
    CHECK_FALSE(click(again, session, 20.1, 0));
    CHECK(again.cancel());
}

TEST_CASE("dragging draws a line, and dragging back from its end a tangent arc") {
    Session session = emptySketch();
    SketchTool tool(SketchToolKind::Line, "f1", false);
    CHECK(drag(tool, session, 0, 0, 10, 0));
    CHECK(sketchOf(session).lines.size() == 1);

    // From (10, 0), leaving along +x, to (10, 10): a half circle around (10, 5), to the left.
    CHECK(drag(tool, session, 10, 0, 10, 10));
    const Sketch& sketch = sketchOf(session);
    REQUIRE(sketch.arcs.size() == 1);
    const SketchArc& arc = sketch.arcs[0];
    CHECK(at(session, arc.center) == std::pair{10.0, 5.0});
    CHECK(at(session, arc.start) == std::pair{10.0, 0.0});
    CHECK(at(session, arc.end) == std::pair{10.0, 10.0});

    // The polyline goes on from the arc's end, and the next drag back is tangent to the arc:
    // leaving (10, 10) along -x.
    click(tool, session, 0, 10);
    CHECK(sketchOf(session).lines.back().start == arc.end);
    click(tool, session, 0, 0.2);  // closes on the first point
    CHECK(extrudedArea(session) == doctest::Approx(100 + std::numbers::pi * 25 / 2));
}

TEST_CASE("rectangles: two corners, center and corner, three points") {
    Session session = emptySketch();
    SketchTool corners(SketchToolKind::RectangleTwoPoint, "f1", false);
    CHECK_FALSE(click(corners, session, 4, 3));
    CHECK(click(corners, session, 0, 0));  // either diagonal: the result runs counter-clockwise
    CHECK(sketchOf(session).lines.size() == 4);
    CHECK(constraintTypes(session) ==
          std::vector<std::string>{"horizontal", "vertical", "horizontal", "vertical"});
    const Regeneration& one = session.regenerate();
    REQUIRE(one.model.sketches.at("f1")->regions.size() == 1);
    CHECK(one.model.sketches.at("f1")->regions[0].name.find('-') == std::string::npos);
    CHECK(extrudedArea(session) == doctest::Approx(12));

    Session centered = emptySketch();
    SketchTool center(SketchToolKind::RectangleCenter, "f1", false);
    clicks(center, centered, {{5, 5}, {7, 6}});
    CHECK(extrudedArea(centered) == doctest::Approx(8));

    Session tilted = emptySketch();
    SketchTool three(SketchToolKind::RectangleThreePoint, "f1", false);
    CHECK_FALSE(click(three, tilted, 0, 0));
    CHECK_FALSE(click(three, tilted, 3, 4));  // one side, 5 long
    CHECK(click(three, tilted, -4, 3));       // 5 wide
    CHECK(extrudedArea(tilted) == doctest::Approx(25));
    CHECK(constraintTypes(tilted) ==
          std::vector<std::string>{"perpendicular", "parallel", "parallel"});

    // A drag from corner to corner works too; a flat rectangle draws nothing.
    Session dragged = emptySketch();
    SketchTool byDrag(SketchToolKind::RectangleTwoPoint, "f1", false);
    CHECK_FALSE(drag(byDrag, dragged, 0, 0, 6, 0));
    CHECK(drag(byDrag, dragged, 0, 0, 2, 5));
    CHECK(extrudedArea(dragged) == doctest::Approx(10));
}

TEST_CASE("circles: center and radius, diameter, three points") {
    const double pi = std::numbers::pi;
    Session session = emptySketch();
    SketchTool center(SketchToolKind::CircleCenter, "f1", false);
    clicks(center, session, {{0, 0}, {3, 0}});
    CHECK(extrudedArea(session) == doctest::Approx(pi * 9));

    Session diameter = emptySketch();
    SketchTool two(SketchToolKind::CircleTwoPoint, "f1", false);
    clicks(two, diameter, {{0, 0}, {4, 0}});
    CHECK(at(diameter, sketchOf(diameter).circles[0].center) == std::pair{2.0, 0.0});

    Session through = emptySketch();
    SketchTool three(SketchToolKind::CircleThreePoint, "f1", false);
    CHECK_FALSE(click(three, through, 0, 0));
    CHECK_FALSE(click(three, through, 2, 0));
    CHECK_FALSE(click(three, through, 4, 0));  // on one line: no circle; the tool waits
    CHECK(click(three, through, 1, 1));
    const auto [cx, cy] = at(through, sketchOf(through).circles[0].center);
    CHECK(cx == doctest::Approx(1));
    CHECK(cy == doctest::Approx(0));
    CHECK(sketchOf(through).circles[0].radius.number == doctest::Approx(1));
}

TEST_CASE("arcs: three points, center, tangent") {
    // Through (0, 1): counter-clockwise from (1, 0) to (-1, 0).
    Session upper = emptySketch();
    SketchTool three(SketchToolKind::ArcThreePoint, "f1", false);
    clicks(three, upper, {{1, 0}, {-1, 0}, {0, 1}});
    REQUIRE(sketchOf(upper).arcs.size() == 1);
    CHECK(at(upper, sketchOf(upper).arcs[0].start) == std::pair{1.0, 0.0});

    // Through (0, -1): clockwise, stored counter-clockwise from (-1, 0) to (1, 0).
    Session lower = emptySketch();
    SketchTool down(SketchToolKind::ArcThreePoint, "f1", false);
    clicks(down, lower, {{1, 0}, {-1, 0}, {0, -1}});
    CHECK(at(lower, sketchOf(lower).arcs[0].start) == std::pair{-1.0, 0.0});

    // By center: the way round follows the pointer.
    for (const double side : {1.0, -1.0}) {
        CAPTURE(side);
        Session session = emptySketch();
        SketchTool center(SketchToolKind::ArcCenter, "f1", false);
        clicks(center, session, {{0, 0}, {2, 0}});
        for (const double angle : {0.5, 1.2, 2.0, 2.8}) {
            center.pointer(session, PointerEvent::Move, 2 * std::cos(angle),
                           side * 2 * std::sin(angle), kSnap);
        }
        CHECK(click(center, session, -2, 0.01 * side));
        const SketchArc& arc = sketchOf(session).arcs.at(0);
        const auto [su, sv] = at(session, arc.start);
        CHECK(su == doctest::Approx(side > 0 ? 2.0 : -2.0).epsilon(1e-3));
        static_cast<void>(sv);
    }

    // Tangent: from the end of a line.
    Session tangent = emptySketch();
    SketchTool line(SketchToolKind::Line, "f1", false);
    clicks(line, tangent, {{0, 0}, {10, 0}});
    line.cancel();
    SketchTool arc(SketchToolKind::ArcTangent, "f1", false);
    CHECK_FALSE(click(arc, tangent, 5, 5));  // not the end of a curve: nothing starts
    CHECK_FALSE(click(arc, tangent, 10.1, 0));
    CHECK(click(arc, tangent, 10, 10));
    REQUIRE(sketchOf(tangent).arcs.size() == 1);
    CHECK(at(tangent, sketchOf(tangent).arcs[0].center) == std::pair{10.0, 5.0});
}

TEST_CASE("points snap to existing ones, and construction and units carry through") {
    Session session = emptySketch(LengthUnit::Inch);
    SketchTool point(SketchToolKind::Point, "f1", false);
    CHECK(click(point, session, 25.4, 0));
    CHECK_FALSE(click(point, session, 25.5, 0));  // that point exists
    REQUIRE(sketchOf(session).points.size() == 1);
    CHECK(sketchOf(session).points[0].x.number == doctest::Approx(1));  // in inches

    // A polyline from the point uses it; construction lines are marked.
    SketchTool line(SketchToolKind::Line, "f1", true);
    clicks(line, session, {{25.6, 0.1}, {50.8, 0}});
    const Sketch& sketch = sketchOf(session);
    REQUIRE(sketch.lines.size() == 1);
    CHECK(sketch.lines[0].start == sketch.points[0].id);
    CHECK(sketch.lines[0].construction);
    CHECK(sketch.points.size() == 2);

    CHECK_THROWS_AS(
        [&] {
            SketchTool lost(SketchToolKind::Point, "f9", false);
            click(lost, session, 0, 0);
        }(),
        Error);
}

TEST_CASE("the preview shows what the next click draws") {
    Session session = emptySketch();
    SketchTool line(SketchToolKind::Line, "f1", false);
    CHECK(line.preview(session, 0.01).edgeIds.empty());
    click(line, session, 0, 0);
    line.pointer(session, PointerEvent::Move, 5, 5, kSnap);
    const Mesh preview = line.preview(session, 0.01);
    CHECK(preview.edgeIds.size() == 1);
    CHECK(preview.vertexIds.size() == 2);  // the start and the pointer
    CHECK(preview.edgePoints[3] == doctest::Approx(5));

    SketchTool rectangle(SketchToolKind::RectangleTwoPoint, "f1", false);
    rectangle.pointer(session, PointerEvent::Down, 0, 0, kSnap);
    rectangle.pointer(session, PointerEvent::Move, 3, 2, kSnap);
    CHECK(rectangle.preview(session, 0.01).edgeIds.size() == 4);  // while dragging
}

TEST_CASE("drawing in a sketch transaction is one undo step") {
    Session session = emptySketch();
    const std::size_t steps = session.undoCount();
    session.beginTransaction("Sketch");
    SketchTool rectangle(SketchToolKind::RectangleTwoPoint, "f1", false);
    clicks(rectangle, session, {{0, 0}, {2, 2}});
    SketchTool circle(SketchToolKind::CircleCenter, "f1", false);
    clicks(circle, session, {{5, 5}, {6, 5}});
    session.commitTransaction();
    CHECK(session.undoCount() == steps + 1);
    REQUIRE(session.undo());
    CHECK(sketchOf(session).lines.empty());
    CHECK(sketchOf(session).circles.empty());
}

TEST_CASE("curves are made construction curves and back") {
    Session session = emptySketch();
    SketchTool rectangle(SketchToolKind::RectangleTwoPoint, "f1", false);
    clicks(rectangle, session, {{0, 0}, {2, 2}});
    session.edit([](Document& document) {
        applyEdits(document,
                   R"({"op": "setConstruction", "sketch": "f1", "entities": ["l7", "l9"],
                       "construction": true})");
    });
    const Sketch& sketch = sketchOf(session);
    CHECK(sketch.lines[0].construction);
    CHECK_FALSE(sketch.lines[1].construction);
    CHECK(sketch.lines[2].construction);
    CHECK(editName(R"({"op": "setConstruction", "construction": true})") == "Make construction");
    // Two sides left: no closed profile any more.
    CHECK(session.regenerate().features[1].status == FeatureStatus::Error);
    CHECK_THROWS_AS(session.edit([](Document& document) {
        applyEdits(document, R"({"op": "setConstruction", "sketch": "f1", "entities": ["p3"],
                                 "construction": true})");
    }),
                    Error);
}
