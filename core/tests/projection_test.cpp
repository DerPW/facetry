// Projecting origin geometry and edges into sketches (#52, docs/adr/0018-sketch-projection.md).
#include "facetry/core/error.hpp"
#include "facetry/core/session.hpp"
#include "facetry/core/sketch_edit.hpp"
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

const Sketch& sketchIn(const Session& session, const std::string& id) {
    return std::get<Sketch>(session.document().findFeature(id)->data);
}

std::pair<double, double> at(const Session& session, const std::string& sketch,
                             const std::string& point) {
    for (const SketchPoint& p : sketchIn(session, sketch).points) {
        if (p.id == point) {
            return {p.x.number, p.y.number};
        }
    }
    FAIL("no point " << point);
    return {};
}

// The block (width × depth × height) with an empty sketch f5 on its top face.
Session blockWithTopSketch() {
    Session session(rectangleBlock());
    session.regenerate();
    const NamedShape& block = *session.regeneration().model.bodies.at("f2");
    const auto top = block.find(ElementKind::Face, "f2:end");
    REQUIRE(top);
    session.edit([&](Document& document) {
        Sketch sketch;
        sketch.plane = "face";
        sketch.face = {makeReference(session.regeneration().model, "f2", ElementKind::Face, *top)};
        document.root.timeline.push_back(feature("f5", "Sketch 2", sketch));
    });
    session.regenerate();
    return session;
}

std::size_t project(Session& session, const std::string& sketch,
                    std::vector<ProjectionSource> sources) {
    std::size_t added = 0;
    session.edit([&](Document& document) {
        added = projectIntoSketch(document, session.regeneration().model, sketch, sources);
    });
    return added;
}

// The extent of the points a sketch's projections drive, in document units.
struct Extent {
    double minU = HUGE_VAL;
    double minV = HUGE_VAL;
    double maxU = -HUGE_VAL;
    double maxV = -HUGE_VAL;
};

Extent projectedExtent(const Session& session, const std::string& sketch) {
    Extent extent;
    for (const SketchProjection& projection : sketchIn(session, sketch).projections) {
        for (const std::string& point : projection.points) {
            const auto [u, v] = at(session, sketch, point);
            extent = {std::min(extent.minU, u), std::min(extent.minV, v), std::max(extent.maxU, u),
                      std::max(extent.maxV, v)};
        }
    }
    return extent;
}

}  // namespace

TEST_CASE("Projecting a top face draws an offset rectangle that follows the block") {
    Session session = blockWithTopSketch();
    const NamedShape& block = *session.regeneration().model.bodies.at("f2");
    const std::uint32_t top = *block.find(ElementKind::Face, "f2:end");
    REQUIRE(project(session, "f5", {{.body = "f2", .kind = ElementKind::Face, .id = top}}) == 4);
    // Projecting again adds nothing.
    CHECK(project(session, "f5", {{.body = "f2", .kind = ElementKind::Face, .id = top}}) == 0);

    const Sketch& sketch = sketchIn(session, "f5");
    CHECK(sketch.projections.size() == 4);
    CHECK(sketch.lines.size() == 4);
    CHECK(sketch.points.size() == 8);
    const Regeneration& first = session.regenerate();
    CHECK(first.features.back().status == FeatureStatus::Ok);
    REQUIRE(first.model.sketches.at("f5")->regions.size() == 1);
    Extent outline = projectedExtent(session, "f5");
    CHECK(outline.maxU - outline.minU == doctest::Approx(kWidth).epsilon(1e-9));

    // The rectangle tool, one millimeter inside the projected outline: its corners snap to
    // nothing, so it is free, and the outline and it bound two regions.
    SketchTool rectangle(SketchToolKind::RectangleTwoPoint, "f5", false);
    for (const auto& [u, v] : {std::pair{outline.minU + 1, outline.minV + 1},
                               std::pair{outline.maxU - 1, outline.maxV - 1}}) {
        rectangle.pointer(session, PointerEvent::Down, u, v, 0.1);
        rectangle.pointer(session, PointerEvent::Up, u, v, 0.1);
    }
    const Regeneration& drawn = session.regenerate();
    const auto& regions = drawn.model.sketches.at("f5")->regions;
    REQUIRE(regions.size() == 2);

    // Extruding the inner region raises a boss on the block.
    // The inner one is bounded by four curves, the ring by eight.
    const auto inner = std::find_if(regions.begin(), regions.end(), [](const SketchRegion& r) {
        return std::count(r.name.begin(), r.name.end(), '|') == 3;
    });
    REQUIRE(inner != regions.end());
    session.edit([&](Document& document) {
        document.root.timeline.push_back(
            feature("f20", "Extrude 2",
                    Extrude{.profile = "f5", .distance = 2.0, .regions = {inner->name}}));
    });
    const Regeneration& boss = session.regenerate();
    CHECK(boss.features.back().status == FeatureStatus::Ok);
    CHECK(boss.model.bodies.at("f20")->shape.properties().volume ==
          doctest::Approx((kWidth - 2) * (kDepth - 2) * 2));

    // A wider block: the projected edges follow, the drawn rectangle stays.
    setParameter(session, "width", 14);
    const Regeneration& wider = session.regenerate();
    CHECK(wider.features[2].status == FeatureStatus::Ok);
    outline = projectedExtent(session, "f5");
    CHECK(outline.maxU - outline.minU == doctest::Approx(14).epsilon(1e-9));
    CHECK(wider.model.bodies.at("f20")->shape.properties().volume ==
          doctest::Approx((kWidth - 2) * (kDepth - 2) * 2));

    // What projections drive does not change the cache: nothing runs again.
    CHECK(session.regenerate().executed == 0);
}

TEST_CASE("Curves drawn from projected points follow them") {
    Session session = blockWithTopSketch();
    const NamedShape& block = *session.regeneration().model.bodies.at("f2");
    project(session, "f5",
            {{.body = "f2",
              .kind = ElementKind::Face,
              .id = *block.find(ElementKind::Face, "f2:end")}});
    session.regenerate();
    // A line from the projected corner furthest along u to the middle of the face.
    const Sketch& sketch = sketchIn(session, "f5");
    std::string corner = sketch.projections.front().points.front();
    for (const SketchProjection& projection : sketch.projections) {
        for (const std::string& point : projection.points) {
            if (at(session, "f5", point).first > at(session, "f5", corner).first + 1e-9) {
                corner = point;
            }
        }
    }
    const auto [cu, cv] = at(session, "f5", corner);
    const Extent outline = projectedExtent(session, "f5");
    SketchTool line(SketchToolKind::Line, "f5", false);
    for (const auto& [u, v] :
         {std::pair{cu, cv},
          std::pair{(outline.minU + outline.maxU) / 2, (outline.minV + outline.maxV) / 2 + 0.3}}) {
        line.pointer(session, PointerEvent::Down, u, v, 0.1);
        line.pointer(session, PointerEvent::Up, u, v, 0.1);
    }
    const SketchLine& drawn = sketchIn(session, "f5").lines.back();
    const std::string start = drawn.start;
    CHECK(sketchIn(session, "f5").projectionOf(start) != nullptr);

    setParameter(session, "width", 14);
    session.regenerate();
    const Extent wider = projectedExtent(session, "f5");
    CHECK(wider.maxU - wider.minU == doctest::Approx(14));
    const double u = at(session, "f5", start).first;
    CHECK((u == doctest::Approx(wider.minU) || u == doctest::Approx(wider.maxU)));
    static_cast<void>(cu);
}

TEST_CASE("Origin items project as a point and construction axes") {
    Session session(rectangleBlock());
    session.edit([](Document& document) {
        Sketch sketch;
        sketch.plane = "xz";
        document.root.timeline.push_back(feature("f5", "Sketch 2", sketch));
    });
    session.regenerate();
    REQUIRE(project(session, "f5", {{.origin = "o"}, {.origin = "x"}, {.origin = "z"}}) == 3);
    // Y is perpendicular to XZ.
    CHECK_THROWS_AS(project(session, "f5", {{.origin = "y"}}), Error);

    const Sketch& sketch = sketchIn(session, "f5");
    REQUIRE(sketch.projections.size() == 3);
    CHECK(sketch.projections[0].curve.empty());
    CHECK(at(session, "f5", sketch.projections[0].points[0]) == std::pair{0.0, 0.0});
    REQUIRE(sketch.lines.size() == 2);
    CHECK(sketch.lines[0].construction);
    CHECK(at(session, "f5", sketch.lines[0].start) == std::pair{-1000.0, 0.0});
    CHECK(at(session, "f5", sketch.lines[1].end) == std::pair{0.0, 1000.0});
    CHECK(session.regenerate().features.back().status == FeatureStatus::Ok);

    // Snapping finds the projected origin point.
    SketchTool point(SketchToolKind::Point, "f5", false);
    point.pointer(session, PointerEvent::Move, 0.02, 0.01, 0.1);
    CHECK(point.feedback().snap == "point");
}

TEST_CASE("Circles and arcs parallel to the sketch project with their radius") {
    // A cylinder of radius r extruded from a circle on XY, and a sketch on XY projecting its
    // top edge.
    Document document;
    document.parameters = {{"r", 3.0}};
    Sketch base;
    base.points = {{"p1", 1, 2}};
    base.circles = {{"c2", "p1", Value::of("r")}};
    document.root.timeline.push_back(feature("f1", "Sketch 1", base));
    document.root.timeline.push_back(
        feature("f3", "Extrude 1", Extrude{.profile = "f1", .distance = 5.0, .regions = {}}));
    document.root.timeline.push_back(feature("f4", "Sketch 2", Sketch{}));
    Session session(document);
    session.regenerate();
    const NamedShape& cylinder = *session.regeneration().model.bodies.at("f3");
    std::uint32_t top = 0;
    for (std::uint32_t id = 1; id <= cylinder.edges.size(); ++id) {
        const ElementGeometry edge = elementGeometry(cylinder.shape, ElementKind::Edge, id);
        if (edge.type == "circle" && std::abs(edge.centroid.z - 5) < 1e-9) {
            top = id;
        }
    }
    REQUIRE(top != 0);
    REQUIRE(project(session, "f4", {{.body = "f3", .kind = ElementKind::Edge, .id = top}}) == 1);
    const Sketch& sketch = sketchIn(session, "f4");
    REQUIRE(sketch.circles.size() == 1);
    CHECK(sketch.circles[0].radius.number == doctest::Approx(3));
    CHECK(at(session, "f4", sketch.circles[0].center) == std::pair{1.0, 2.0});

    setParameter(session, "r", 4);
    session.regenerate();
    CHECK(sketchIn(session, "f4").circles[0].radius.number == doctest::Approx(4));

    // A straight edge standing up projects to a point: it cannot be projected.
    std::uint32_t seam = 0;
    for (std::uint32_t id = 1; id <= cylinder.edges.size(); ++id) {
        if (elementGeometry(cylinder.shape, ElementKind::Edge, id).type == "line") {
            seam = id;
        }
    }
    if (seam != 0) {
        CHECK_THROWS_AS(
            project(session, "f4", {{.body = "f3", .kind = ElementKind::Edge, .id = seam}}), Error);
    }
}

TEST_CASE("Projections break with a warning and come apart when their curve is deleted") {
    Session session(rectangleBlock());
    session.edit([](Document& document) {
        document.root.timeline.push_back(feature("f5", "Sketch 2", Sketch{}));
    });
    session.regenerate();
    const NamedShape& block = *session.regeneration().model.bodies.at("f2");
    // A horizontal edge of the bottom face, along y = 0.
    std::uint32_t edge = 0;
    for (std::uint32_t id = 1; id <= block.edges.size(); ++id) {
        const ElementGeometry geometry = elementGeometry(block.shape, ElementKind::Edge, id);
        if (geometry.type == "line" && std::abs(geometry.centroid.y) < 1e-9 &&
            std::abs(geometry.centroid.z) < 1e-9) {
            edge = id;
        }
    }
    REQUIRE(edge != 0);
    REQUIRE(project(session, "f5", {{.body = "f2", .kind = ElementKind::Edge, .id = edge}}) == 1);
    session.regenerate();
    const std::string projectedLine = sketchIn(session, "f5").projections[0].curve;
    const auto start = at(session, "f5", sketchIn(session, "f5").projections[0].points[0]);

    // Edits leave it: trim and move do nothing to it.
    session.edit([&](Document& document) {
        CHECK_FALSE(trimSketch(document, "f5", kWidth / 2, 0.05, 0.2));
        moveSketchEntities(document, "f5", {projectedLine}, 5, 5, false);
    });
    CHECK(at(session, "f5", sketchIn(session, "f5").projections[0].points[0]) == start);

    // Without the extrusion the edge is gone: a warning, and the line stays where it was.
    session.edit([](Document& document) { document.root.timeline[1].suppressed = true; });
    const Regeneration& lost = session.regenerate();
    CHECK(lost.features.back().status == FeatureStatus::Warning);
    CHECK(lost.features.back().brokenReferences == std::vector<std::size_t>{0});
    CHECK(at(session, "f5", sketchIn(session, "f5").projections[0].points[0]) == start);

    // Deleting the projected line removes the projection and its points.
    session.edit(
        [&](Document& document) { deleteSketchEntities(document, "f5", {projectedLine}); });
    CHECK(sketchIn(session, "f5").projections.empty());
    CHECK(sketchIn(session, "f5").points.empty());
    CHECK(session.regenerate().features.back().status == FeatureStatus::Ok);
}

TEST_CASE("Projections are saved and checked") {
    Session session = blockWithTopSketch();
    const NamedShape& block = *session.regeneration().model.bodies.at("f2");
    project(
        session, "f5",
        {{.body = "f2", .kind = ElementKind::Face, .id = *block.find(ElementKind::Face, "f2:end")},
         {.origin = "o"}});
    const Document& document = session.document();
    CHECK(parseDocument(toJson(document)) == document);

    // Bodies made after the sketch cannot be projected into it.
    Session later(rectangleBlock());
    later.edit([](Document& d) {
        d.root.timeline.insert(d.root.timeline.begin() + 1, feature("f5", "Sketch 0", Sketch{}));
    });
    later.regenerate();
    CHECK_THROWS_AS(project(later, "f5", {{.body = "f2", .kind = ElementKind::Edge, .id = 1}}),
                    Error);

    // A projection must drive the points of its curve.
    Document broken = document;
    std::get<Sketch>(broken.findFeature("f5")->data).projections[0].points.pop_back();
    CHECK_THROWS_AS(validate(broken), Error);
}
