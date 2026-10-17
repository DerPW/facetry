// Sketches (docs/adr/0013-sketch-model.md): planes, curves, construction geometry, constraints
// and how a sketch becomes the profile of an extrusion.
#include "facetry/core/error.hpp"
#include "facetry/core/session_json.hpp"

#include "corner_fillet.hpp"

#include <doctest.h>
#include <nlohmann/json.hpp>

#include <cmath>
#include <limits>
#include <numbers>
#include <string>
#include <vector>

using namespace facetry::core;
using namespace facetry::test;

namespace {

constexpr double kPi = std::numbers::pi;

ErrorCode errorCode(const auto& action) {
    try {
        action();
    } catch (const Error& error) {
        return error.code();
    }
    FAIL("expected an error");
    return ErrorCode::Internal;
}

// A w × d rectangle with its corner at the sketch origin, lines l1–l4 through p1–p4.
Sketch rectangle(double w, double d) {
    Sketch sketch;
    sketch.points = {{"p1", 0, 0}, {"p2", w, 0}, {"p3", w, d}, {"p4", 0, d}};
    sketch.lines = {{"l1", "p1", "p2"}, {"l2", "p2", "p3"}, {"l3", "p3", "p4"}, {"l4", "p4", "p1"}};
    return sketch;
}

// A sketch and its extrusion by `height`: features f1 and f2.
Document extruded(Sketch sketch, double height) {
    Document document;
    document.root.timeline.push_back(feature("f1", "Sketch 1", std::move(sketch)));
    document.root.timeline.push_back(
        feature("f2", "Extrude 1", Extrude{.profile = "f1", .distance = height, .regions = {}}));
    return document;
}

const NamedShape& body(const Regeneration& regeneration, const std::string& id) {
    const FeatureResult* result = regeneration.find(id);
    REQUIRE(result != nullptr);
    INFO((result->messages.empty() ? std::string() : result->messages.front()));
    REQUIRE(result->status == FeatureStatus::Ok);
    return *regeneration.model.bodies.at(id);
}

void checkBounds(const NamedShape& shape, Vec3 min, Vec3 max) {
    const BoundingBox bounds = shape.shape.properties().bounds;
    CHECK(bounds.min.x == doctest::Approx(min.x));
    CHECK(bounds.min.y == doctest::Approx(min.y));
    CHECK(bounds.min.z == doctest::Approx(min.z));
    CHECK(bounds.max.x == doctest::Approx(max.x));
    CHECK(bounds.max.y == doctest::Approx(max.y));
    CHECK(bounds.max.z == doctest::Approx(max.z));
}

void checkNear(Vec3 actual, Vec3 expected) {
    CHECK(actual.x == doctest::Approx(expected.x));
    CHECK(actual.y == doctest::Approx(expected.y));
    CHECK(actual.z == doctest::Approx(expected.z));
}

// A slot: two lines of length `length`, 2r apart, joined by half circles of radius r around
// (0, r) and (length, r). With `inward`, the left arc runs from p1 to p4 and bulges inward.
Sketch slot(double length, double r, bool inward = false) {
    Sketch sketch;
    sketch.points = {{"p1", 0, 0},     {"p2", length, 0}, {"p3", length, 2 * r},
                     {"p4", 0, 2 * r}, {"c1", length, r}, {"c2", 0, r}};
    sketch.lines = {{"l1", "p1", "p2"}, {"l2", "p3", "p4"}};
    sketch.arcs = {{"a1", "c1", "p2", "p3"},
                   inward ? SketchArc{"a2", "c2", "p1", "p4"} : SketchArc{"a2", "c2", "p4", "p1"}};
    return sketch;
}

// The rectangle block with a second sketch on its face named `face`: a circle of radius 1
// around (cx, cy) in the face's frame, extruded by 2. Features f3 and f4.
Session faceSketchSession(const std::string& face, double cx, double cy) {
    Session session(rectangleBlock());
    const Regeneration& block = session.regenerate();
    const NamedShape& shape = *block.model.bodies.at("f2");
    const auto id = shape.find(ElementKind::Face, face);
    REQUIRE(id);
    const Reference reference = makeReference(block.model, "f2", ElementKind::Face, *id);
    session.edit([&](Document& document) {
        Sketch sketch;
        sketch.plane = "face";
        sketch.face = {reference};
        sketch.points = {{"p1", cx, cy}};
        sketch.circles = {{"c1", "p1", 1.0}};
        document.root.timeline.push_back(feature("f3", "Sketch 2", sketch));
        document.root.timeline.push_back(
            feature("f4", "Extrude 2", Extrude{.profile = "f3", .distance = 2.0, .regions = {}}));
    });
    return session;
}

}  // namespace

TEST_CASE("sketches lie on the origin planes and extrude along their normals") {
    // XY: up along +Z.
    Session xy(extruded(rectangle(10, 6), 4));
    checkBounds(body(xy.regenerate(), "f2"), {0, 0, 0}, {10, 6, 4});

    // XZ: sketch x along X, y along Z, so the extrusion goes along -Y, as in Fusion.
    Sketch onXz = rectangle(10, 6);
    onXz.plane = "xz";
    Session xz(extruded(onXz, 4));
    const NamedShape& front = body(xz.regenerate(), "f2");
    checkBounds(front, {0, -4, 0}, {10, 0, 6});
    CHECK(front.shape.properties().volume == doctest::Approx(240));

    // YZ: sketch x along Y, y along Z, extruded along +X; a negative distance goes the other way.
    Sketch onYz = rectangle(10, 6);
    onYz.plane = "yz";
    Session yz(extruded(onYz, -4));
    checkBounds(body(yz.regenerate(), "f2"), {-4, 0, 0}, {0, 10, 6});

    CHECK(originPlaneFrame("xz").normal().y == doctest::Approx(-1));
    CHECK(originPlaneFrame("yz").normal().x == doctest::Approx(1));
    CHECK(errorCode([] { static_cast<void>(originPlaneFrame("ab")); }) ==
          ErrorCode::InvalidArgument);
}

TEST_CASE("arcs and lines form one profile") {
    const double length = 8;
    const double r = 2;
    const double height = 3;
    Session session(extruded(slot(length, r), height));
    const NamedShape& outward = body(session.regenerate(), "f2");
    CHECK(outward.shape.properties().volume ==
          doctest::Approx((length * 2 * r + kPi * r * r) * height));
    checkBounds(outward, {-r, 0, 0}, {length + r, 2 * r, height});
    // Every curve names its side face; the arcs' faces are cylinders.
    for (const char* curve : {"l1", "l2", "a1", "a2"}) {
        CAPTURE(curve);
        const auto face = outward.find(ElementKind::Face, std::string("f2:side[f1/") + curve + "]");
        REQUIRE(face);
        const std::string type = elementGeometry(outward.shape, ElementKind::Face, *face).type;
        CHECK(type == (curve[0] == 'a' ? "cylinder" : "plane"));
    }

    // An arc walked from its end to its start keeps its shape: the left half circle bulges in.
    Session inward(extruded(slot(length, r, /*inward=*/true), height));
    CHECK(body(inward.regenerate(), "f2").shape.properties().volume ==
          doctest::Approx(length * 2 * r * height));
}

TEST_CASE("a circle is a profile on its own") {
    Sketch sketch;
    sketch.plane = "yz";
    sketch.points = {{"p1", 5, 5}};
    sketch.circles = {{"c1", "p1", Value::of("radius")}};
    Document document = extruded(sketch, 10);
    document.parameters = {{"radius", 3}};
    Session session(document);
    const NamedShape& cylinder = body(session.regenerate(), "f2");
    CHECK(cylinder.shape.properties().volume == doctest::Approx(kPi * 9 * 10));
    checkBounds(cylinder, {0, 2, 2}, {10, 8, 8});
    CHECK(cylinder.find(ElementKind::Face, "f2:side[f1/c1]"));
    CHECK(cylinder.find(ElementKind::Face, "f2:start"));
    CHECK(cylinder.find(ElementKind::Face, "f2:end"));

    setParameter(session, "radius", 1);
    CHECK(body(session.regenerate(), "f2").shape.properties().volume == doctest::Approx(kPi * 10));

    // A literal radius converts with the units, so the geometry stays.
    session.edit([](Document& edited) {
        std::get<Sketch>(edited.root.timeline[0].data).circles[0].radius = 2.0;
        convertUnits(edited, LengthUnit::Centimeter);
    });
    CHECK(std::get<Sketch>(session.document().root.timeline[0].data).circles[0].radius.number ==
          doctest::Approx(0.2));
    CHECK(body(session.regenerate(), "f2").shape.properties().volume ==
          doctest::Approx(kPi * 4 * 10));
}

TEST_CASE("construction curves never bound a profile") {
    Sketch sketch = rectangle(10, 6);
    sketch.lines.push_back({"l5", "p1", "p3", /*construction=*/true});
    sketch.circles.push_back({"c6", "p1", 2.0, /*construction=*/true});
    Session session(extruded(sketch, 4));
    CHECK(body(session.regenerate(), "f2").shape.properties().volume == doctest::Approx(240));

    // As real geometry, the diagonal splits the rectangle into two triangles: two profiles.
    session.edit([](Document& document) {
        std::get<Sketch>(document.root.timeline[0].data).lines.back().construction = false;
    });
    const Regeneration& diagonal = session.regenerate();
    CHECK(diagonal.features[0].status == FeatureStatus::Ok);
    CHECK(diagonal.features[1].status == FeatureStatus::Error);
    CHECK(diagonal.features[1].messages.at(0).find("several profiles") != std::string::npos);

    // A real circle next to the rectangle is a second profile.
    session.edit([](Document& document) {
        auto& edited = std::get<Sketch>(document.root.timeline[0].data);
        edited.lines.back().construction = true;
        edited.circles.back().construction = false;
    });
    const Regeneration& twoProfiles = session.regenerate();
    CHECK(twoProfiles.features[1].status == FeatureStatus::Error);
    CHECK(twoProfiles.features[1].messages.at(0).find("several profiles") != std::string::npos);

    // Only construction geometry: nothing to extrude.
    session.edit([](Document& document) {
        auto& edited = std::get<Sketch>(document.root.timeline[0].data);
        for (SketchLine& line : edited.lines) {
            line.construction = true;
        }
        edited.circles.clear();
    });
    CHECK(session.regenerate().features[1].messages.at(0).find("no closed profile") !=
          std::string::npos);
}

TEST_CASE("a sketch on a planar face follows the face") {
    Session session = faceSketchSession("f2:end", 3, 2);
    const Regeneration& stacked = session.regenerate();
    const NamedShape& block = body(stacked, "f2");
    const NamedShape& boss = body(stacked, "f4");
    CHECK(boss.shape.properties().volume == doctest::Approx(kPi * 2));
    const BoundingBox bounds = boss.shape.properties().bounds;
    CHECK(bounds.min.z == doctest::Approx(kHeight));
    CHECK(bounds.max.z == doctest::Approx(kHeight + 2));
    CHECK(stacked.features[2].brokenReferences.empty());

    // The face's frame: on the face, its normal out of the body, its axes a right-handed basis.
    const SketchFrame top = planarFaceFrame(block, *block.find(ElementKind::Face, "f2:end"));
    CHECK(top.origin.z == doctest::Approx(kHeight));
    checkNear(top.normal(), {0, 0, 1});
    const SketchFrame bottom = planarFaceFrame(block, *block.find(ElementKind::Face, "f2:start"));
    checkNear(bottom.normal(), {0, 0, -1});
    const SketchFrame side =
        planarFaceFrame(block, *block.find(ElementKind::Face, "f2:side[f1/l1]"));
    checkNear(side.normal(), {0, -1, 0});
    CHECK(errorCode([&] { static_cast<void>(planarFaceFrame(block, 99)); }) ==
          ErrorCode::InvalidArgument);

    // A taller block moves the sketch and its extrusion up.
    setParameter(session, "height", 10);
    const Regeneration& taller = session.regenerate();
    CHECK(body(taller, "f4").shape.properties().bounds.min.z == doctest::Approx(10));
    CHECK(taller.executed == 3);  // the first sketch comes from the cache

    // Deleting the block's sketch breaks everything after it, the face sketch included.
    const auto state = nlohmann::json::parse(sessionStateJson(session));
    CHECK(state["features"][0]["dependents"] == nlohmann::json::array({"f2", "f3", "f4"}));
    CHECK(state["features"][1]["dependents"] == nlohmann::json::array({"f3", "f4"}));
    CHECK(state["features"][2]["definition"]["plane"] == "face");
}

TEST_CASE("a sketch on a face fails when the face is lost or not planar") {
    // The extrusion's side faces are planes; the round of a fillet is not.
    Session session = cornerFilletSession();
    const Regeneration& rounded = session.regenerate();
    const NamedShape& shape = *rounded.model.bodies.at("f2");
    const Reference round =
        makeReference(rounded.model, "f2", ElementKind::Face, roundFace(shape, kFillet));
    session.edit([&](Document& document) {
        Sketch sketch;
        sketch.plane = "face";
        sketch.face = {round};
        document.root.timeline.push_back(feature("f6", "Sketch 2", sketch));
    });
    const Regeneration& curved = session.regenerate();
    CHECK(curved.features[3].status == FeatureStatus::Error);
    CHECK(curved.features[3].messages.at(0).find("planar face") != std::string::npos);

    // A reference to a body that is gone.
    session.edit([](Document& document) {
        std::get<Sketch>(document.root.timeline[3].data).face[0].body = "f9";
    });
    const Regeneration& lost = session.regenerate();
    CHECK(lost.features[3].status == FeatureStatus::Error);
    CHECK(lost.features[3].brokenReferences == std::vector<std::size_t>{0});
}

TEST_CASE("sketch structure is validated on every edit") {
    const auto rejected = [](auto change) {
        Session session(extruded(slot(8, 2), 3));
        return errorCode([&] {
            session.edit([&](Document& document) {
                change(std::get<Sketch>(document.root.timeline[0].data));
            });
        });
    };
    const auto invalid = ErrorCode::InvalidArgument;
    CHECK(rejected([](Sketch& s) { s.plane = "ab"; }) == invalid);
    CHECK(rejected([](Sketch& s) { s.plane = "face"; }) == invalid);  // no face reference
    CHECK(rejected([](Sketch& s) {
              s.face = {Reference{
                  .body = "f2", .kind = ElementKind::Face, .name = "f2:end", .shadow = {}}};
          }) == invalid);  // a face reference on an origin plane
    CHECK(rejected([](Sketch& s) {
              s.plane = "face";
              s.face = {
                  Reference{.body = "f2", .kind = ElementKind::Edge, .name = "e", .shadow = {}}};
          }) == invalid);
    CHECK(rejected([](Sketch& s) { s.arcs[0].end = "p9"; }) == invalid);
    CHECK(rejected([](Sketch& s) { s.arcs[0].end = s.arcs[0].start; }) == invalid);
    CHECK(rejected([](Sketch& s) { s.arcs[0].id = "p1"; }) == invalid);  // IDs are shared
    CHECK(rejected([](Sketch& s) { s.circles.push_back({"c9", "p9", 1.0}); }) == invalid);
    CHECK(rejected([](Sketch& s) {
              s.circles.push_back({"c9", "p1", std::numeric_limits<double>::quiet_NaN()});
          }) == invalid);
    CHECK(rejected([](Sketch& s) { s.constraints.push_back({"k1", "glued", {"p1", "p2"}}); }) ==
          invalid);
    CHECK(rejected([](Sketch& s) { s.constraints.push_back({"k1", "horizontal", {}}); }) ==
          invalid);
    CHECK(rejected([](Sketch& s) {
              s.constraints.push_back({"k1", "coincident", {"p1", "p2", "p3"}});
          }) == invalid);
    CHECK(rejected([](Sketch& s) { s.constraints.push_back({"k1", "fix", {"p9"}}); }) == invalid);
    CHECK(rejected([](Sketch& s) {
              s.constraints.push_back({"k1", "fix", {"p1"}});
              s.constraints.push_back({"k2", "fix", {"k1"}});  // constraints bind geometry only
          }) == invalid);

    // Valid constraints are kept (the solver comes with M1.4) and IDs continue after them.
    Session session(extruded(slot(8, 2), 3));
    session.edit([](Document& document) {
        auto& sketch = std::get<Sketch>(document.root.timeline[0].data);
        sketch.constraints = {{"k7", "horizontal", {"l1"}},
                              {"k8", "tangent", {"l1", "a1"}},
                              {"k9", "coincident", {"c1", "l1"}}};
    });
    Document constrained = session.document();
    CHECK(constrained.newId("k") == "k10");
}

TEST_CASE("every sketch field round-trips through JSON") {
    Sketch sketch = slot(8, 2);
    sketch.plane = "xz";
    sketch.lines.push_back({"l3", "p1", "p3", /*construction=*/true});
    sketch.circles = {{"c3", "c1", Value::of("radius"), /*construction=*/true}};
    sketch.constraints = {{"k1", "parallel", {"l1", "l2"}}, {"k2", "fix", {"p1"}}};
    Document document = extruded(sketch, 3);
    document.parameters = {{"radius", 1}};
    const std::string saved = toJson(document);
    CHECK(parseDocument(saved) == document);
    CHECK(toJson(parseDocument(saved)) == saved);
    CHECK(saved.find("\"plane\": \"xz\"") != std::string::npos);
    CHECK(saved.find("\"construction\": true") != std::string::npos);

    // Defaults stay out of the file: no face, no empty lists, no construction flag when false.
    const std::string plain = toJson(extruded(rectangle(1, 1), 1));
    CHECK(plain.find("\"plane\": \"xy\"") != std::string::npos);
    for (const char* absent :
         {"\"face\"", "\"arcs\"", "\"circles\"", "\"constraints\"", "\"construction\""}) {
        CAPTURE(absent);
        CHECK(plain.find(absent) == std::string::npos);
    }
}
