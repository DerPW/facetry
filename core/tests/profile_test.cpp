// Sketch profiles (docs/adr/0014-sketch-profiles.md): the regions a sketch's curves enclose, their
// names, and extruding the ones chosen.
#include "facetry/core/error.hpp"

#include "corner_fillet.hpp"

#include <doctest.h>

#include <algorithm>
#include <numbers>
#include <string>
#include <vector>

using namespace facetry::core;
using namespace facetry::test;

namespace {

constexpr double kPi = std::numbers::pi;

// A 10 × 6 rectangle (l1–l4 counter-clockwise through p1–p4) with a circle c6 of radius 2 around
// p5 = (5, 3) inside it, extruded by 4 (features f1 and f2).
Document plateWithCircle() {
    Sketch sketch;
    sketch.points = {{"p1", 0, 0}, {"p2", 10, 0}, {"p3", 10, 6}, {"p4", 0, 6}, {"p5", 5, 3}};
    sketch.lines = {{"l1", "p1", "p2"}, {"l2", "p2", "p3"}, {"l3", "p3", "p4"}, {"l4", "p4", "p1"}};
    sketch.circles = {{"c6", "p5", 2.0}};
    Document document;
    document.root.timeline.push_back(feature("f1", "Sketch 1", sketch));
    document.root.timeline.push_back(
        feature("f2", "Extrude 1", Extrude{.profile = "f1", .distance = 4.0, .regions = {}}));
    return document;
}

const std::string kDisc = "region{f1/c6+}";
const std::string kPlate = "region{f1/c6-|f1/l1+|f1/l2+|f1/l3+|f1/l4+}";

Sketch& sketchOf(Document& document) {
    return std::get<Sketch>(document.root.timeline[0].data);
}

std::vector<std::string> regionNames(const Regeneration& regeneration) {
    std::vector<std::string> names;
    for (const SketchRegion& region : regeneration.model.sketches.at("f1")->regions) {
        names.push_back(region.name);
    }
    return names;
}

void chooseRegions(Session& session, std::vector<std::string> regions) {
    session.edit([&](Document& document) {
        std::get<Extrude>(document.root.timeline[1].data).regions = regions;
    });
}

const NamedShape& extruded(const Regeneration& regeneration) {
    const FeatureResult& result = regeneration.features.at(1);
    INFO((result.messages.empty() ? std::string() : result.messages.front()));
    REQUIRE(result.status == FeatureStatus::Ok);
    return *regeneration.model.bodies.at("f2");
}

}  // namespace

TEST_CASE("a rectangle with a circle inside extrudes as a plate with a hole or as the disc") {
    Session session(plateWithCircle());
    const Regeneration& both = session.regenerate();
    CHECK(regionNames(both) == std::vector{kDisc, kPlate});
    CHECK(both.features[1].status == FeatureStatus::Error);
    CHECK(both.features[1].messages.at(0).find("several profiles") != std::string::npos);

    chooseRegions(session, {kPlate});
    const NamedShape& plate = extruded(session.regenerate());
    CHECK(plate.shape.properties().volume == doctest::Approx((60 - 4 * kPi) * 4));
    CHECK(plate.faces.size() == 7);
    CHECK(plate.find(ElementKind::Face, "f2:side[f1/c6]"));
    CHECK(plate.find(ElementKind::Face, "f2:side[f1/l1]"));
    CHECK(plate.find(ElementKind::Face, "f2:start"));
    CHECK(plate.find(ElementKind::Face, "f2:end"));
    CHECK(plate.find(ElementKind::Edge, "edge{f2:end|f2:side[f1/c6]}"));

    chooseRegions(session, {kDisc});
    const NamedShape& disc = extruded(session.regenerate());
    CHECK(disc.shape.properties().volume == doctest::Approx(4 * kPi * 4));
    CHECK(disc.faces.size() == 3);

    // Adjacent regions merge: the circle between them leaves no trace.
    chooseRegions(session, {kPlate, kDisc});
    const NamedShape& block = extruded(session.regenerate());
    CHECK(block.shape.properties().volume == doctest::Approx(240));
    CHECK(block.faces.size() == 6);
    CHECK_FALSE(block.find(ElementKind::Face, "f2:side[f1/c6]"));
}

TEST_CASE("region names follow the curves around them, not unrelated edits") {
    Session session(plateWithCircle());
    chooseRegions(session, {kPlate});
    CHECK(regionNames(session.regenerate()) == std::vector{kDisc, kPlate});

    // A circle elsewhere and a different size keep both names.
    session.edit([](Document& document) {
        Sketch& sketch = sketchOf(document);
        sketch.points.push_back({"p7", 20, 3});
        sketch.circles.push_back({"c8", "p7", 1.0});
        sketch.points[1].x = 12.0;
        sketch.points[2].x = 12.0;
    });
    const Regeneration& edited = session.regenerate();
    CHECK(regionNames(edited) == std::vector{kDisc, kPlate, std::string("region{f1/c8+}")});
    CHECK(extruded(edited).shape.properties().volume == doctest::Approx((72 - 4 * kPi) * 4));

    // Drawing the rectangle the other way round turns its sides: the old name is gone and the
    // extrusion says so.
    session.edit([](Document& document) {
        for (SketchLine& line : sketchOf(document).lines) {
            std::swap(line.start, line.end);
        }
    });
    const Regeneration& reversed = session.regenerate();
    CHECK(regionNames(reversed).at(1) == "region{f1/c6-|f1/l1-|f1/l2-|f1/l3-|f1/l4-}");
    CHECK(reversed.features[1].status == FeatureStatus::Error);
    CHECK(reversed.features[1].messages.at(0).find("no longer in the sketch") != std::string::npos);
}

TEST_CASE("crossing curves split each other into regions") {
    // Two overlapping circles: two crescents and the lens between them.
    Sketch circles;
    circles.points = {{"p1", 0, 0}, {"p2", 3, 0}};
    circles.circles = {{"c3", "p1", 2.0}, {"c4", "p2", 2.0}};
    Document document;
    document.root.timeline.push_back(feature("f1", "Sketch 1", circles));
    document.root.timeline.push_back(
        feature("f2", "Extrude 1",
                Extrude{.profile = "f1", .distance = 1.0, .regions = {"region{f1/c3+|f1/c4+}"}}));
    Session session(document);
    const Regeneration& lens = session.regenerate();
    CHECK(regionNames(lens) == std::vector<std::string>{"region{f1/c3+|f1/c4+}",
                                                        "region{f1/c3+|f1/c4-}",
                                                        "region{f1/c3-|f1/c4+}"});
    // The lens of two circles of radius r whose centers are d apart.
    const double r = 2;
    const double d = 3;
    const double area = 2 * r * r * std::acos(d / (2 * r)) - d / 2 * std::sqrt(4 * r * r - d * d);
    CHECK(extruded(lens).shape.properties().volume == doctest::Approx(area));

    // A line across the rectangle: the left part lies left of the line going up.
    Session split(plateWithCircle());
    split.edit([](Document& edited) {
        Sketch& sketch = sketchOf(edited);
        sketch.circles.clear();
        sketch.points.push_back({"p7", 2, -1});
        sketch.points.push_back({"p8", 2, 7});
        sketch.lines.push_back({"l9", "p7", "p8"});
        std::get<Extrude>(edited.root.timeline[1].data).regions = {
            "region{f1/l1+|f1/l3+|f1/l4+|f1/l9+}"};
    });
    const Regeneration& left = split.regenerate();
    CHECK(regionNames(left) == std::vector<std::string>{"region{f1/l1+|f1/l2+|f1/l3+|f1/l9-}",
                                                        "region{f1/l1+|f1/l3+|f1/l4+|f1/l9+}"});
    const NamedShape& strip = extruded(left);
    CHECK(strip.shape.properties().volume == doctest::Approx(2 * 6 * 4));
    CHECK(strip.find(ElementKind::Face, "f2:side[f1/l9]"));

    // Both parts merge into the whole block; the split pieces of l1 and l3 join again.
    chooseRegions(split,
                  {"region{f1/l1+|f1/l2+|f1/l3+|f1/l9-}", "region{f1/l1+|f1/l3+|f1/l4+|f1/l9+}"});
    const NamedShape& whole = extruded(split.regenerate());
    CHECK(whole.shape.properties().volume == doctest::Approx(240));
    CHECK(whole.faces.size() == 6);
    CHECK(whole.find(ElementKind::Face, "f2:side[f1/l1]"));
}

TEST_CASE("islands nest and open curves bound nothing") {
    // A ring between two circles inside the rectangle, and a line hanging into the plate.
    Session session(plateWithCircle());
    session.edit([](Document& document) {
        Sketch& sketch = sketchOf(document);
        sketch.circles.push_back({"c7", "p5", 1.0});
        sketch.points.push_back({"p8", 0.5, 0.5});
        sketch.points.push_back({"p9", 1.5, 1.0});
        sketch.lines.push_back({"l10", "p8", "p9"});
    });
    const Regeneration& nested = session.regenerate();
    CHECK(regionNames(nested) == std::vector<std::string>{"region{f1/c6+|f1/c7-}",
                                                          "region{f1/c6-|f1/l1+|f1/l2+|f1/l3+|"
                                                          "f1/l4+}",
                                                          "region{f1/c7+}"});

    // The plate and the inner disc do not touch: one body of two solids. The hanging line
    // leaves no face inside the plate.
    chooseRegions(session, {kPlate, "region{f1/c7+}"});
    const NamedShape& body = extruded(session.regenerate());
    CHECK(body.shape.properties().counts.solids == 2);
    CHECK(body.shape.properties().volume == doctest::Approx((60 - 4 * kPi + kPi) * 4));
    CHECK_FALSE(body.find(ElementKind::Face, "f2:side[f1/l10]"));

    // A line from the middle of l1 into the plate splits l1 where it starts; the extrusion still
    // has one side face for l1.
    session.edit([](Document& document) {
        Sketch& sketch = sketchOf(document);
        sketch.circles.clear();
        sketch.points.push_back({"p11", 5, 0});
        sketch.lines.push_back({"l12", "p11", "p5"});
        std::get<Extrude>(document.root.timeline[1].data).regions.clear();
    });
    const NamedShape& block = extruded(session.regenerate());
    CHECK(block.faces.size() == 6);
    CHECK(block.find(ElementKind::Face, "f2:side[f1/l1]"));
}

TEST_CASE("an extrusion's profiles are validated and saved") {
    Session session(plateWithCircle());
    const auto rejected = [&](std::vector<std::string> regions) {
        try {
            chooseRegions(session, std::move(regions));
        } catch (const Error& error) {
            return error.code() == ErrorCode::InvalidArgument;
        }
        return false;
    };
    CHECK(rejected({kDisc, kDisc}));
    CHECK(rejected({""}));

    chooseRegions(session, {kPlate});
    const std::string saved = session.save();
    CHECK(saved.find("\"regions\": [\n          \"" + kPlate + "\"") != std::string::npos);
    CHECK(Session::load(saved).document() == session.document());
}
