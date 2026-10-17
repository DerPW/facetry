// Sketch editing (core/include/facetry/core/sketch_edit.hpp).
#include "facetry/core/sketch_edit.hpp"

#include "facetry/core/error.hpp"
#include "facetry/core/session.hpp"
#include "facetry/core/session_json.hpp"
#include "facetry/core/sketch_tool.hpp"

#include "corner_fillet.hpp"

#include <doctest.h>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <string>
#include <utility>
#include <vector>

using namespace facetry::core;
using namespace facetry::test;

namespace {

constexpr double kPick = 0.5;
constexpr double kPi = std::numbers::pi;

// An empty sketch f1 on XY and an extrusion of it by 2.
Document sketchDocument(LengthUnit units = LengthUnit::Millimeter) {
    Document document;
    document.units = units;
    document.root.timeline.push_back(feature("f1", "Sketch 1", Sketch{}));
    document.root.timeline.push_back(
        feature("f2", "Extrude 1", Extrude{.profile = "f1", .distance = 2.0, .regions = {}}));
    return document;
}

Sketch& sketchOf(Document& document) {
    return std::get<Sketch>(document.root.timeline[0].data);
}

std::string point(Document& document, double x, double y) {
    std::string id = document.newId("p");
    sketchOf(document).points.push_back({.id = id, .x = x, .y = y});
    return id;
}

std::string line(Document& document, const std::string& start, const std::string& end) {
    std::string id = document.newId("l");
    sketchOf(document).lines.push_back({.id = id, .start = start, .end = end});
    return id;
}

std::string line(Document& document, double x0, double y0, double x1, double y1) {
    const std::string start = point(document, x0, y0);
    return line(document, start, point(document, x1, y1));
}

void constrain(Document& document, std::string type, std::vector<std::string> entities) {
    sketchOf(document).constraints.push_back(
        {.id = document.newId("k"), .type = std::move(type), .entities = std::move(entities)});
}

// A closed polygon of lines through `corners`; returns its points.
std::vector<std::string> polygon(Document& document,
                                 const std::vector<std::pair<double, double>>& corners) {
    std::vector<std::string> points;
    for (const auto& [x, y] : corners) {
        points.push_back(point(document, x, y));
    }
    for (std::size_t i = 0; i < points.size(); ++i) {
        line(document, points[i], points[(i + 1) % points.size()]);
    }
    return points;
}

// The position of a sketch point, in document units.
std::pair<double, double> at(Document& document, const std::string& id) {
    for (const SketchPoint& p : sketchOf(document).points) {
        if (p.id == id) {
            return {p.x.number, p.y.number};
        }
    }
    FAIL("no point " << id);
    return {};
}

void checkAt(Document& document, const std::string& id, double x, double y) {
    const auto [u, v] = at(document, id);
    INFO(id << " at (" << u << ", " << v << ")");
    CHECK(u == doctest::Approx(x));
    CHECK(v == doctest::Approx(y));
}

const SketchLine& lineOf(Document& document, const std::string& id) {
    const auto& lines = sketchOf(document).lines;
    const auto it =
        std::find_if(lines.begin(), lines.end(), [&](const SketchLine& l) { return l.id == id; });
    REQUIRE(it != lines.end());
    return *it;
}

double extrudedArea(const Document& document) {
    Session session(document);
    const Regeneration& regeneration = session.regenerate();
    const FeatureResult& extrude = regeneration.features.at(1);
    INFO((extrude.messages.empty() ? std::string() : extrude.messages.front()));
    REQUIRE(extrude.status == FeatureStatus::Ok);
    return regeneration.model.bodies.at("f2")->shape.properties().volume / 2.0;
}

std::size_t count(const Document& document, const std::string& type) {
    const Sketch& sketch = std::get<Sketch>(document.root.timeline[0].data);
    return static_cast<std::size_t>(
        std::count_if(sketch.constraints.begin(), sketch.constraints.end(),
                      [&](const SketchConstraint& c) { return c.type == type; }));
}

}  // namespace

TEST_CASE("Trimming the overshoot of a line ends it on the curve it crosses") {
    Document document = sketchDocument();
    // Three sides of a square, and a left side that runs on below the bottom.
    const std::string corner = point(document, 0, 0);
    const std::string right = point(document, 10, 0);
    const std::string top = point(document, 10, 10);
    const std::string left = point(document, 0, 10);
    line(document, corner, right);
    line(document, right, top);
    line(document, top, left);
    const std::string side = line(document, 0, 10, 0, -4);
    const std::string overshoot = lineOf(document, side).end;

    REQUIRE(trimSketch(document, "f1", 0.2, -3, kPick));

    // The side now ends at the square's corner: the point there, not a new one.
    CHECK(lineOf(document, side).end == corner);
    const auto& points = sketchOf(document).points;
    CHECK(std::none_of(points.begin(), points.end(),
                       [&](const SketchPoint& p) { return p.id == overshoot; }));
    CHECK(extrudedArea(document) == doctest::Approx(100.0));
}

TEST_CASE("Trimming between two crossing lines splits a line and keeps its direction") {
    Document document = sketchDocument();
    const std::string base = line(document, 0, 0, 10, 0);
    constrain(document, "horizontal", {base});
    const std::string first = line(document, 3, -2, 3, 2);
    const std::string second = line(document, 7, -2, 7, 2);

    REQUIRE(trimSketch(document, "f1", 5, 0.1, kPick));

    const Sketch& sketch = sketchOf(document);
    REQUIRE(sketch.lines.size() == 4);
    checkAt(document, lineOf(document, base).end, 3, 0);
    const SketchLine& piece = sketch.lines.back();
    checkAt(document, piece.start, 7, 0);
    checkAt(document, piece.end, 10, 0);
    CHECK(count(document, "horizontal") == 2);
    // The new ends stay on the lines that cut them.
    CHECK(count(document, "coincident") == 2);
    const auto has = [&](const std::vector<std::string>& entities) {
        return std::any_of(sketch.constraints.begin(), sketch.constraints.end(),
                           [&](const SketchConstraint& c) { return c.entities == entities; });
    };
    CHECK(has({lineOf(document, base).end, first}));
    CHECK(has({piece.start, second}));
}

TEST_CASE("Trimming a line that nothing crosses deletes it with its points") {
    Document document = sketchDocument();
    const std::string lone = line(document, 0, 0, 10, 0);
    constrain(document, "horizontal", {lone});
    line(document, 0, 5, 10, 5);

    REQUIRE(trimSketch(document, "f1", 5, 0.2, kPick));
    CHECK(sketchOf(document).lines.size() == 1);
    CHECK(sketchOf(document).points.size() == 2);
    CHECK(sketchOf(document).constraints.empty());

    CHECK_FALSE(trimSketch(document, "f1", 5, 2.5, kPick));
}

TEST_CASE("Trimming a circle cut twice leaves an arc with the circle's ID") {
    Document document = sketchDocument();
    const std::string center = point(document, 0, 0);
    sketchOf(document).circles.push_back({.id = "c90", .center = center, .radius = 5.0});
    const std::string across = line(document, -8, 0, 8, 0);

    REQUIRE(trimSketch(document, "f1", 0, 5.1, kPick));

    const Sketch& sketch = sketchOf(document);
    CHECK(sketch.circles.empty());
    REQUIRE(sketch.arcs.size() == 1);
    const SketchArc& arc = sketch.arcs.front();
    CHECK(arc.id == "c90");
    // Counter-clockwise from the left cut round the bottom to the right one.
    checkAt(document, arc.start, -5, 0);
    checkAt(document, arc.end, 5, 0);
    CHECK(extrudedArea(document) == doctest::Approx(25.0 * kPi / 2).epsilon(1e-6));

    // Trimming the line's ends outside the arc leaves the half disk's outline.
    REQUIRE(trimSketch(document, "f1", -7, 0.1, kPick));
    REQUIRE(trimSketch(document, "f1", 7, 0.1, kPick));
    checkAt(document, lineOf(document, across).start, -5, 0);
    CHECK(lineOf(document, across).start == arc.start);
    CHECK(lineOf(document, across).end == sketchOf(document).arcs.front().end);
    CHECK(extrudedArea(document) == doctest::Approx(25.0 * kPi / 2).epsilon(1e-6));
}

TEST_CASE("Trimming a circle cut once or not at all deletes it") {
    Document document = sketchDocument();
    const std::string center = point(document, 0, 0);
    sketchOf(document).circles.push_back({.id = "c90", .center = center, .radius = 5.0});
    line(document, 5, -3, 5, 3);  // tangent: one cut

    REQUIRE(trimSketch(document, "f1", -5, 0, kPick));
    CHECK(sketchOf(document).circles.empty());
    CHECK(sketchOf(document).points.size() == 2);
}

TEST_CASE("Trimming an arc between two cuts splits it") {
    Document document = sketchDocument();
    const std::string center = point(document, 0, 0);
    const std::string start = point(document, 5, 0);
    const std::string end = point(document, -5, 0);
    sketchOf(document).arcs.push_back({.id = "a90", .center = center, .start = start, .end = end});
    line(document, 3, -1, 3, 6);
    line(document, -3, -1, -3, 6);

    REQUIRE(trimSketch(document, "f1", 0, 5.1, kPick));

    const Sketch& sketch = sketchOf(document);
    REQUIRE(sketch.arcs.size() == 2);
    CHECK(sketch.arcs[0].start == start);
    checkAt(document, sketch.arcs[0].end, 3, 4);
    checkAt(document, sketch.arcs[1].start, -3, 4);
    CHECK(sketch.arcs[1].end == end);
    CHECK(sketch.arcs[1].center == center);
}

TEST_CASE("Extending a line runs it to the first curve ahead") {
    Document document = sketchDocument();
    const std::string short_ = line(document, 0, 0, 4, 0);
    const std::string near = line(document, 10, -5, 10, 5);
    line(document, 20, -5, 20, 5);
    const std::string center = point(document, -10, 0);
    sketchOf(document).circles.push_back({.id = "c90", .center = center, .radius = 3.0});

    REQUIRE(extendSketch(document, "f1", 3.5, 0.1, kPick));
    checkAt(document, lineOf(document, short_).end, 10, 0);
    CHECK(sketchOf(document).constraints.back().entities ==
          std::vector{lineOf(document, short_).end, near});

    // The start runs to the circle's near side.
    REQUIRE(extendSketch(document, "f1", 0.5, 0.1, kPick));
    const auto [u, v] = at(document, lineOf(document, short_).start);
    CHECK(u == doctest::Approx(-7.0));
    CHECK(v == doctest::Approx(0.0));

    // Nothing lies ahead of the vertical line's ends.
    CHECK_FALSE(extendSketch(document, "f1", 10, 4.5, kPick));
    CHECK_FALSE(extendSketch(document, "f1", 0, 30, kPick));
}

TEST_CASE("Filleting the corners of an L profile rounds them with tangent arcs") {
    Document document = sketchDocument();
    const std::vector<std::string> corners =
        polygon(document, {{0, 0}, {20, 0}, {20, 5}, {5, 5}, {5, 20}, {0, 20}});
    const double lArea = 20 * 5 + 5 * 15;
    REQUIRE(extrudedArea(document) == doctest::Approx(lArea));

    // A convex corner loses s² − πr²/4, a concave one gains it; at 90° r equals the setback.
    const double corner = 4.0 - kPi;
    REQUIRE(filletSketchCorner(document, "f1", corners[1], 2.0));
    CHECK(extrudedArea(document) == doctest::Approx(lArea - corner).epsilon(1e-6));
    REQUIRE(filletSketchCorner(document, "f1", corners[3], 2.0));
    CHECK(extrudedArea(document) == doctest::Approx(lArea).epsilon(1e-6));

    const Sketch& sketch = sketchOf(document);
    REQUIRE(sketch.arcs.size() == 2);
    CHECK(count(document, "tangent") == 4);
    // The corner points are gone; the arcs run from (18, 0) to (20, 2) round (18, 2).
    CHECK(std::none_of(sketch.points.begin(), sketch.points.end(),
                       [&](const SketchPoint& p) { return p.id == corners[1]; }));
    const SketchArc& arc = sketch.arcs.front();
    checkAt(document, arc.center, 18, 2);
    checkAt(document, arc.start, 18, 0);
    checkAt(document, arc.end, 20, 2);
}

TEST_CASE("Filleting needs a corner of two lines that are long enough") {
    Document document = sketchDocument();
    const std::vector<std::string> corners = polygon(document, {{0, 0}, {10, 0}, {10, 3}});
    line(document, corners[0], point(document, -5, 5));  // a third line at corners[0]

    CHECK_FALSE(filletSketchCorner(document, "f1", corners[0], 1.0));
    CHECK_FALSE(filletSketchCorner(document, "f1", corners[1], 3.0));  // longer than a side
    CHECK_FALSE(filletSketchCorner(document, "f1", corners[1], 0.0));
    CHECK(filletSketchCorner(document, "f1", corners[1], 1.0));
    // An acute corner: the arc starts the setback away along each line.
    CHECK(filletSketchCorner(document, "f1", corners[2], 1.0));
    const SketchArc& arc = sketchOf(document).arcs.back();
    const auto [su, sv] = at(document, arc.start);
    const auto [eu, ev] = at(document, arc.end);
    const auto [cu, cv] = at(document, arc.center);
    CHECK(std::hypot(su - cu, sv - cv) == doctest::Approx(std::hypot(eu - cu, ev - cv)));
    CHECK(std::hypot(su - 10, sv - 3) == doctest::Approx(1.0));
    CHECK(std::hypot(eu - 10, ev - 3) == doctest::Approx(1.0));
}

TEST_CASE("Deleting points and curves removes what depends on them") {
    Document document = sketchDocument();
    const std::vector<std::string> corners =
        polygon(document, {{0, 0}, {10, 0}, {10, 10}, {0, 10}});
    const Sketch& sketch = sketchOf(document);
    const std::string bottom = sketch.lines[0].id;
    const std::string right = sketch.lines[1].id;
    constrain(document, "horizontal", {bottom});
    constrain(document, "perpendicular", {bottom, right});
    constrain(document, "vertical", {sketch.lines[3].id});
    const std::string lone = line(document, 20, 0, 30, 0);

    // A corner takes both lines through it; their other points stay with the other lines.
    deleteSketchEntities(document, "f1", {corners[1]});
    CHECK(sketch.lines.size() == 3);
    CHECK(sketch.points.size() == 5);
    REQUIRE(sketch.constraints.size() == 1);
    CHECK(sketch.constraints.front().type == "vertical");

    // A line alone takes its points with it.
    deleteSketchEntities(document, "f1", {lone});
    CHECK(sketch.lines.size() == 2);
    CHECK(sketch.points.size() == 3);

    CHECK_THROWS_AS(deleteSketchEntities(document, "f1", {"l999"}), Error);
    CHECK_THROWS_AS(deleteSketchEntities(document, "f9", {corners[0]}), Error);
}

TEST_CASE("Moving and copying sketch entities") {
    Document document = sketchDocument(LengthUnit::Centimeter);
    document.parameters.push_back({.name = "x", .value = 1.0});
    const std::vector<std::string> corners = polygon(document, {{1, 0}, {3, 0}, {3, 2}, {1, 2}});
    sketchOf(document).points.front().x = Value::of("x");
    std::vector<std::string> square;
    for (const SketchLine& l : sketchOf(document).lines) {
        square.push_back(l.id);
    }
    constrain(document, "horizontal", {square[0]});
    constrain(document, "vertical", {square[1]});
    const double area = extrudedArea(document);

    // Moving by 10 mm moves by 1 cm; a parameter becomes a number.
    moveSketchEntities(document, "f1", square, 10, -10, false);
    checkAt(document, corners[0], 2, -1);
    CHECK(sketchOf(document).points.front().x.parameter.empty());
    checkAt(document, corners[2], 4, 1);
    CHECK(extrudedArea(document) == doctest::Approx(area));

    // A copy has new points, curves and constraints.
    moveSketchEntities(document, "f1", square, 50, 0, true);
    const Sketch& sketch = sketchOf(document);
    CHECK(sketch.lines.size() == 8);
    CHECK(sketch.points.size() == 8);
    CHECK(count(document, "horizontal") == 2);
    CHECK(count(document, "vertical") == 2);
    checkAt(document, corners[2], 4, 1);
    checkAt(document, sketch.points.back().id, 7, 1);

    // Moving a point drags the curves on it.
    moveSketchEntities(document, "f1", {corners[2]}, 0, 10, false);
    checkAt(document, corners[2], 4, 2);
}

namespace {

const Sketch& sketchOf(const Session& session) {
    return std::get<Sketch>(session.document().root.timeline[0].data);
}

bool click(SketchTool& tool, Session& session, double u, double v) {
    tool.pointer(session, PointerEvent::Down, u, v, kPick);
    return tool.pointer(session, PointerEvent::Up, u, v, kPick);
}

}  // namespace

TEST_CASE("The trim tool trims what the pointer touches and crosses") {
    Document document = sketchDocument();
    for (const double y : {0.0, 5.0, 10.0}) {
        line(document, 0, y, 10, y);
    }
    line(document, 3, -2, 3, 12);
    line(document, 7, -2, 7, 12);
    Session session(document);
    session.regenerate();
    SketchTool tool(SketchToolKind::Trim, "f1", false);

    // Hovering shows the piece that would go, as a guide.
    CHECK_FALSE(tool.pointer(session, PointerEvent::Move, 5, 10.2, kPick));
    const Mesh preview = tool.preview(session, 0.01);
    REQUIRE(preview.edgeIds.size() == 1);
    CHECK(preview.edgeIds.front() == kPreviewGuideId);

    // A drag down through the middle trims all three lines there.
    CHECK(tool.pointer(session, PointerEvent::Down, 5, 10.2, kPick));
    for (double y = 9.8; y > -1.0; y -= 0.3) {
        tool.pointer(session, PointerEvent::Move, 5, y, kPick);
    }
    CHECK_FALSE(tool.pointer(session, PointerEvent::Up, 5, -1, kPick));
    const Sketch& sketch = sketchOf(session);
    CHECK(sketch.lines.size() == 8);
    for (const SketchLine& l : sketch.lines) {
        double x0 = 0;
        double x1 = 0;
        for (const SketchPoint& p : sketch.points) {
            x0 = p.id == l.start ? p.x.number : x0;
            x1 = p.id == l.end ? p.x.number : x1;
        }
        CHECK_FALSE((std::min(x0, x1) < 5.0 && std::max(x0, x1) > 5.0));
    }
}

TEST_CASE("The extend tool extends the line clicked near its end") {
    Document document = sketchDocument();
    const std::string short_ = line(document, 0, 0, 4, 0);
    line(document, 10, -5, 10, 5);
    Session session(document);
    session.regenerate();
    SketchTool tool(SketchToolKind::Extend, "f1", false);

    tool.pointer(session, PointerEvent::Move, 3.8, 0.1, kPick);
    CHECK(tool.preview(session, 0.01).edgeIds.size() == 1);
    // A drag does not extend; a click does.
    tool.pointer(session, PointerEvent::Down, 3.8, 0.1, kPick);
    CHECK_FALSE(tool.pointer(session, PointerEvent::Up, 3.8, 3, kPick));
    CHECK(click(tool, session, 3.8, 0.1));
    const Sketch& sketch = sketchOf(session);
    for (const SketchPoint& p : sketch.points) {
        if (p.id == sketch.lines.front().end) {
            CHECK(p.x.number == doctest::Approx(10.0));
        }
    }
}

TEST_CASE("The fillet tool rounds a corner by the distance dragged from it") {
    Document document = sketchDocument();
    polygon(document, {{0, 0}, {10, 0}, {10, 10}, {0, 10}});
    Session session(document);
    session.regenerate();
    SketchTool tool(SketchToolKind::Fillet, "f1", false);

    // Pressing elsewhere than at a corner does nothing.
    CHECK_FALSE(click(tool, session, 5, 5));
    tool.pointer(session, PointerEvent::Down, 10, 0, kPick);
    tool.pointer(session, PointerEvent::Move, 10, 2, kPick);
    CHECK(tool.feedback().radius == doctest::Approx(2.0));
    CHECK_FALSE(tool.preview(session, 0.01).edgeIds.empty());
    CHECK(tool.pointer(session, PointerEvent::Up, 10, 2, kPick));
    CHECK(sketchOf(session).arcs.size() == 1);
    CHECK(tool.feedback().radius == 0.0);

    // Clicking a corner twice repeats the last size.
    CHECK_FALSE(click(tool, session, 0, 0));
    CHECK(click(tool, session, 0, 0));
    CHECK(sketchOf(session).arcs.size() == 2);
    CHECK(extrudedArea(session.document()) == doctest::Approx(100 - 2 * (4 - kPi)).epsilon(1e-6));
}

TEST_CASE("The move and copy tools move the selection or what is under the pointer") {
    Document document = sketchDocument();
    polygon(document, {{0, 0}, {10, 0}, {10, 10}, {0, 10}});
    Session session(document);
    session.regenerate();

    // Without a selection, dragging a line moves it; the sides stretch.
    SketchTool move(SketchToolKind::Move, "f1", false);
    move.pointer(session, PointerEvent::Down, 5, 0, kPick);
    move.pointer(session, PointerEvent::Move, 5, -2, kPick);
    CHECK(move.preview(session, 0.01).edgeIds.size() == 2);  // the moved line and a guide
    CHECK(move.pointer(session, PointerEvent::Up, 5, -3, kPick));
    CHECK(extrudedArea(session.document()) == doctest::Approx(130.0));

    // With a selection, a base point and a target copy it.
    SketchTool copy(SketchToolKind::Copy, "f1", false);
    std::vector<std::string> square;
    for (const SketchLine& l : sketchOf(session).lines) {
        square.push_back(l.id);
    }
    copy.setSelection(square);
    CHECK_FALSE(click(copy, session, 0, 10));
    CHECK(click(copy, session, 20, 10));
    CHECK(sketchOf(session).lines.size() == 8);
    CHECK(sketchOf(session).points.size() == 8);
}

TEST_CASE("Sketch entities are deleted by a session edit") {
    Document document = sketchDocument();
    polygon(document, {{0, 0}, {10, 0}, {10, 10}, {0, 10}});
    const std::string lone = line(document, 20, 0, 30, 0);
    REQUIRE(lone == "l13");
    Session session(document);
    session.edit([](Document& edited) {
        applyEdits(edited,
                   R"({"op": "deleteSketchEntities", "sketch": "f1", "entities": ["l13"]})");
    });
    CHECK(sketchOf(session).lines.size() == 4);
    CHECK(sketchOf(session).points.size() == 4);
    CHECK(editName(R"({"op": "deleteSketchEntities"})") == "Delete sketch entities");
    CHECK_THROWS_AS(session.edit([](Document& edited) {
        applyEdits(edited,
                   R"({"op": "deleteSketchEntities", "sketch": "f1", "entities": ["l13"]})");
    }),
                    Error);
    CHECK_THROWS_AS(session.edit([](Document& edited) {
        applyEdits(edited, R"({"op": "deleteSketchEntities", "sketch": "f2", "entities": ["l5"]})");
    }),
                    Error);
}

TEST_CASE("The copy tool copies a lone point dragged away") {
    Document document = sketchDocument();
    point(document, 1, 1);
    Session session(document);
    session.regenerate();
    SketchTool copy(SketchToolKind::Copy, "f1", false);
    copy.pointer(session, PointerEvent::Down, 1, 1, kPick);
    copy.pointer(session, PointerEvent::Move, 2, 1, kPick);
    static_cast<void>(copy.preview(session, 0.01));
    CHECK(copy.pointer(session, PointerEvent::Up, 4, 1, kPick));
    CHECK(sketchOf(session).points.size() == 2);
    static_cast<void>(copy.preview(session, 0.01));
    session.regenerate();
    static_cast<void>(copy.preview(session, 0.01));
}
