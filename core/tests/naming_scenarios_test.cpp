// The M0.6 scenarios (docs/roadmap/phase-0-foundation.md): edits that must keep the fillet on
// the edge the user picked. They run on macOS, in the iOS simulator and in WebAssembly.
#include "corner_fillet.hpp"

#include <doctest.h>

#include <algorithm>
#include <string>

using namespace facetry::core;
using namespace facetry::test;

namespace {

constexpr double kBlockVolume = kWidth * kDepth * kHeight;

const Fillet& filletOf(const Session& session) {
    return std::get<Fillet>(session.document().root.timeline.back().data);
}

}  // namespace

TEST_CASE("the fillet rounds the picked edge") {
    Session session = cornerFilletSession();
    const Regeneration& regeneration = session.regenerate();
    CHECK(regeneration.executed == 1);  // the sketch and the extrude come from the cache
    expectRoundAt(regeneration, kFillet, kWidth, kDepth, kHeight, kRadius, kBlockVolume);
    CHECK(filletOf(session).edges[0].name == "edge{f2:side[f1/l2]|f2:side[f1/l3]}");
}

TEST_CASE("scenario: change the rectangle width") {
    Session session = cornerFilletSession();
    session.regenerate();
    for (const double width : {14.0, 7.0}) {
        setParameter(session, "width", width);
        const Regeneration& regeneration = session.regenerate();
        CHECK(regeneration.executed == 3);
        expectRoundAt(regeneration, kFillet, width, kDepth, kHeight, kRadius,
                      width * kDepth * kHeight);
    }
}

TEST_CASE("scenario: change the extrude height") {
    Session session = cornerFilletSession();
    session.regenerate();
    for (const double height : {7.0, 2.5}) {
        setParameter(session, "height", height);
        const Regeneration& regeneration = session.regenerate();
        CHECK(regeneration.executed == 2);  // the sketch does not depend on the height
        expectRoundAt(regeneration, kFillet, kWidth, kDepth, height, kRadius,
                      kWidth * kDepth * height);
    }
}

TEST_CASE(
    "scenario: replace the rectangle with a polygon that has one more edge before the "
    "filleted one") {
    Session session = cornerFilletSession();
    session.regenerate();
    const std::uint32_t idBefore = filletOf(session).edges[0].shadow->id;

    // Cut off the corner at p2 = (width, 0): p2 moves to (width - 2, 0), a new point p5 sits at
    // (width, 2), and a new line from p2 to p5 comes before l2, which now starts at p5.
    session.edit([](Document& document) {
        auto& sketch = std::get<Sketch>(document.findFeature("f1")->data);
        const std::string point = document.newId("p");
        const std::string line = document.newId("l");
        sketch.points[1].x = kWidth - 2.0;
        sketch.points.push_back({point, Value::of("width"), 2.0});
        sketch.lines.insert(sketch.lines.begin() + 1, {line, "p2", point});
        sketch.lines[2].start = point;
    });
    const Regeneration& regeneration = session.regenerate();
    CHECK(regeneration.model.bodies.at("f2")->faces.size() == 8);
    expectRoundAt(regeneration, kFillet, kWidth, kDepth, kHeight, kRadius,
                  kBlockVolume - 2.0 * kHeight);

    // The edge moved to another index: a reference stored as an index would now point
    // somewhere else (see "indices shift when a sketch edge is inserted" in naming_test.cpp).
    CHECK(filletOf(session).edges[0].shadow->id != idBefore);
}

TEST_CASE("scenario: delete the fillet, undo, regenerate") {
    Session session = cornerFilletSession();
    session.regenerate();

    session.edit([](Document& document) { document.root.timeline.pop_back(); });
    const Regeneration& deleted = session.regenerate();
    CHECK(deleted.features.size() == 2);
    CHECK(roundFace(*deleted.model.bodies.at("f2"), kFillet) == 0);
    CHECK(deleted.model.bodies.at("f2")->shape.properties().volume ==
          doctest::Approx(kBlockVolume));

    REQUIRE(session.undo());
    const Regeneration& restored = session.regenerate();
    CHECK(restored.executed == 0);  // the fillet's earlier result is still cached
    expectRoundAt(restored, kFillet, kWidth, kDepth, kHeight, kRadius, kBlockVolume);

    REQUIRE(session.redo());
    CHECK(roundFace(*session.regenerate().model.bodies.at("f2"), kFillet) == 0);
    REQUIRE(session.undo());
    expectRoundAt(session.regenerate(), kFillet, kWidth, kDepth, kHeight, kRadius, kBlockVolume);
}

TEST_CASE("scenario: save, reload, regenerate") {
    Session session = cornerFilletSession();
    session.regenerate();
    const std::string saved = session.save();

    Session reloaded = Session::load(saved);
    CHECK(reloaded.document() == session.document());
    const Regeneration& regeneration = reloaded.regenerate();
    CHECK(regeneration.executed == 3);
    expectRoundAt(regeneration, kFillet, kWidth, kDepth, kHeight, kRadius, kBlockVolume);
    CHECK(reloaded.save() == saved);

    // The reloaded document stays editable: the fillet follows the corner.
    setParameter(reloaded, "depth", 9.0);
    expectRoundAt(reloaded.regenerate(), kFillet, kWidth, 9.0, kHeight, kRadius,
                  kWidth * 9.0 * kHeight);
}

TEST_CASE("a redrawn sketch line falls back to the edge's last position, with a warning") {
    Session session = cornerFilletSession();
    session.regenerate();
    // Delete l3 and draw it again: same place, new ID, so the edge's name is gone.
    session.edit([](Document& document) {
        auto& sketch = std::get<Sketch>(document.findFeature("f1")->data);
        sketch.lines[2].id = document.newId("l");
    });
    const Regeneration& regeneration = session.regenerate();
    const FeatureResult* fillet = regeneration.find(kFillet);
    REQUIRE(fillet != nullptr);
    CHECK(fillet->status == FeatureStatus::Warning);
    REQUIRE(fillet->messages.size() == 1);
    CHECK(fillet->messages[0].find("no longer exists") != std::string::npos);
    CHECK(fillet->brokenReferences.empty());
    const NamedShape& body = *regeneration.model.bodies.at("f2");
    const std::uint32_t round = roundFace(body, kFillet);
    REQUIRE(round != 0);
    const ElementGeometry geometry = elementGeometry(body.shape, ElementKind::Face, round);
    CHECK(geometry.centroid.x ==
          doctest::Approx(kWidth - kRadius + 2.0 * kRadius / std::numbers::pi));
    // The name is kept: only the user confirms a new edge.
    CHECK(filletOf(session).edges[0].name == "edge{f2:side[f1/l2]|f2:side[f1/l3]}");
}

TEST_CASE("a reference to an edge that is really gone is reported, not rebound") {
    Session session = cornerFilletSession();
    session.regenerate();
    // Turn the rectangle into a triangle: the corner p3 and its lines disappear.
    session.edit([](Document& document) {
        auto& sketch = std::get<Sketch>(document.findFeature("f1")->data);
        sketch.points.erase(sketch.points.begin() + 2);
        sketch.lines.erase(sketch.lines.begin() + 1, sketch.lines.begin() + 3);
        sketch.lines.insert(sketch.lines.begin() + 1, {document.newId("l"), "p2", "p4"});
    });
    const Regeneration& regeneration = session.regenerate();
    const FeatureResult* fillet = regeneration.find(kFillet);
    REQUIRE(fillet != nullptr);
    CHECK(fillet->status == FeatureStatus::Error);
    CHECK(fillet->brokenReferences == std::vector<std::size_t>{0});
    REQUIRE_FALSE(fillet->messages.empty());
    CHECK(fillet->messages[0].find("edge{f2:side[f1/l2]|f2:side[f1/l3]}") != std::string::npos);
    // The fillet is skipped; the model before it passes through.
    const NamedShape& body = *regeneration.model.bodies.at("f2");
    CHECK(roundFace(body, kFillet) == 0);
    CHECK(body.shape.properties().volume == doctest::Approx(kWidth * kDepth * kHeight / 2.0));
}
