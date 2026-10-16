// Persistent naming in the kernel adapter (docs/adr/0006-persistent-naming.md).
#include "facetry/core/naming.hpp"

#include "facetry/core/error.hpp"

#include <doctest.h>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <set>
#include <string>
#include <vector>

using namespace facetry::core;

namespace {

// A closed loop through `points`, edge i named "f1/l<i+1>".
std::vector<ProfileSegment> loop(const std::vector<std::pair<double, double>>& points) {
    std::vector<ProfileSegment> segments;
    for (std::size_t i = 0; i < points.size(); ++i) {
        const auto& [x0, y0] = points[i];
        const auto& [x1, y1] = points[(i + 1) % points.size()];
        segments.push_back({"f1/l" + std::to_string(i + 1), x0, y0, x1, y1});
    }
    return segments;
}

std::vector<ProfileSegment> rectangle(double width, double depth) {
    return loop({{0, 0}, {width, 0}, {width, depth}, {0, depth}});
}

bool allDistinct(const std::vector<std::string>& names) {
    return std::set<std::string>(names.begin(), names.end()).size() == names.size();
}

const std::string kCornerEdge = "edge{f2:side[f1/l2]|f2:side[f1/l3]}";

}  // namespace

TEST_CASE("an extruded rectangle names its faces by role and sketch edge") {
    const auto segments = rectangle(10, 6);
    const NamedShape body = extrudeProfile(segments, 4, "f2");

    CHECK(body.shape.properties().volume == doctest::Approx(240.0));
    const std::set<std::string> faces(body.faces.begin(), body.faces.end());
    CHECK(faces == std::set<std::string>{"f2:start", "f2:end", "f2:side[f1/l1]", "f2:side[f1/l2]",
                                         "f2:side[f1/l3]", "f2:side[f1/l4]"});
    CHECK(body.edges.size() == 12);
    CHECK(body.vertices.size() == 8);
    CHECK(allDistinct(body.edges));
    CHECK(allDistinct(body.vertices));

    // The caps are where the extrusion starts and ends.
    const auto start = body.find(ElementKind::Face, "f2:start");
    const auto end = body.find(ElementKind::Face, "f2:end");
    REQUIRE(start);
    REQUIRE(end);
    CHECK(elementGeometry(body.shape, ElementKind::Face, *start).centroid.z ==
          doctest::Approx(0.0));
    CHECK(elementGeometry(body.shape, ElementKind::Face, *end).centroid.z == doctest::Approx(4.0));

    // l2 runs from (10, 0) to (10, 6); its side face is the plane x = 10.
    const auto side = body.find(ElementKind::Face, "f2:side[f1/l2]");
    REQUIRE(side);
    const ElementGeometry sideGeometry = elementGeometry(body.shape, ElementKind::Face, *side);
    CHECK(sideGeometry.type == "plane");
    CHECK(sideGeometry.centroid.x == doctest::Approx(10.0));
    CHECK(sideGeometry.size == doctest::Approx(24.0));

    // The vertical edge between l2 and l3 stands at the corner (10, 6).
    const auto corner = body.find(ElementKind::Edge, kCornerEdge);
    REQUIRE(corner);
    const ElementGeometry edge = elementGeometry(body.shape, ElementKind::Edge, *corner);
    CHECK(edge.type == "line");
    CHECK(edge.centroid.x == doctest::Approx(10.0));
    CHECK(edge.centroid.y == doctest::Approx(6.0));
    CHECK(edge.size == doctest::Approx(4.0));
    CHECK(body.find(ElementKind::Vertex, "vertex{f2:end|f2:side[f1/l2]|f2:side[f1/l3]}"));
}

TEST_CASE("profiles extrude the same way in either direction and orientation") {
    auto clockwise = rectangle(10, 6);
    std::reverse(clockwise.begin(), clockwise.end());
    for (auto& segment : clockwise) {
        std::swap(segment.x0, segment.x1);
        std::swap(segment.y0, segment.y1);
    }
    for (const auto& segments : {rectangle(10, 6), clockwise}) {
        for (const double distance : {4.0, -4.0}) {
            const NamedShape body = extrudeProfile(segments, distance, "f2");
            CHECK(body.shape.properties().volume == doctest::Approx(240.0));
            const auto end = body.find(ElementKind::Face, "f2:end");
            REQUIRE(end);
            CHECK(elementGeometry(body.shape, ElementKind::Face, *end).centroid.z ==
                  doctest::Approx(distance));
        }
    }
}

TEST_CASE("invalid profiles are reported") {
    CHECK_THROWS_AS(static_cast<void>(extrudeProfile(rectangle(10, 6), 0.0, "f2")), Error);
    auto open = rectangle(10, 6);
    open.back().x1 = 1.0;
    CHECK_THROWS_AS(static_cast<void>(extrudeProfile(open, 4.0, "f2")), Error);
    const auto twoEdges = loop({{0, 0}, {1, 0}});
    CHECK_THROWS_AS(static_cast<void>(extrudeProfile(twoEdges, 4.0, "f2")), Error);
}

TEST_CASE("a fillet keeps face names and names its round after the edge") {
    const NamedShape body = extrudeProfile(rectangle(10, 6), 4, "f2");
    const auto corner = body.find(ElementKind::Edge, kCornerEdge);
    REQUIRE(corner);
    const std::uint32_t edge = *corner;
    const NamedShape rounded = filletEdges(body, {&edge, 1}, 1.5, "f3");

    const double removed = (1.0 - std::numbers::pi / 4.0) * 1.5 * 1.5 * 4.0;
    CHECK(rounded.shape.properties().volume == doctest::Approx(240.0 - removed));
    CHECK(rounded.faces.size() == 7);
    CHECK(allDistinct(rounded.faces));
    CHECK(allDistinct(rounded.edges));
    CHECK(allDistinct(rounded.vertices));
    for (const auto& face : body.faces) {
        CHECK(rounded.find(ElementKind::Face, face));
    }

    const auto round = rounded.find(ElementKind::Face, "f3:fillet[" + kCornerEdge + "]");
    REQUIRE(round);
    const ElementGeometry geometry = elementGeometry(rounded.shape, ElementKind::Face, *round);
    CHECK(geometry.type == "cylinder");
    // A quarter cylinder around (10 - r, 6 - r): its centroid lies 2r/π further out on x and y.
    const double offset = 1.5 - 2.0 * 1.5 / std::numbers::pi;
    CHECK(geometry.centroid.x == doctest::Approx(10.0 - offset));
    CHECK(geometry.centroid.y == doctest::Approx(6.0 - offset));
    CHECK(geometry.centroid.z == doctest::Approx(2.0));
}

TEST_CASE("rounds meeting at a corner are named after the shared vertex") {
    const NamedShape body = extrudeProfile(rectangle(10, 6), 4, "f2");
    std::vector<std::uint32_t> edges;
    for (const char* name : {"edge{f2:side[f1/l2]|f2:side[f1/l3]}", "edge{f2:end|f2:side[f1/l2]}",
                             "edge{f2:end|f2:side[f1/l3]}"}) {
        const auto id = body.find(ElementKind::Edge, name);
        REQUIRE(id);
        edges.push_back(*id);
    }
    const NamedShape rounded = filletEdges(body, edges, 1.0, "f3");
    CHECK(allDistinct(rounded.faces));
    CHECK(
        rounded.find(ElementKind::Face, "f3:fillet[vertex{f2:end|f2:side[f1/l2]|f2:side[f1/l3]}]"));
    CHECK(std::none_of(rounded.faces.begin(), rounded.faces.end(),
                       [](const std::string& name) { return name.ends_with(":new"); }));
}

TEST_CASE("indices shift when a sketch edge is inserted, names do not") {
    // This is the topological naming problem: the edge at the corner (10, 6) has a different
    // index once the profile gets one more edge before it.
    const NamedShape before = extrudeProfile(rectangle(10, 6), 4, "f2");
    auto chamfered = loop({{0, 0}, {8, 0}, {10, 2}, {10, 6}, {0, 6}});
    // Keep the sketch IDs of the rectangle's edges; the new edge gets a new ID.
    chamfered[1].name = "f1/l5";
    chamfered[2].name = "f1/l2";
    chamfered[3].name = "f1/l3";
    chamfered[4].name = "f1/l4";
    const NamedShape after = extrudeProfile(chamfered, 4, "f2");

    const auto oldId = before.find(ElementKind::Edge, kCornerEdge);
    const auto newId = after.find(ElementKind::Edge, kCornerEdge);
    REQUIRE(oldId);
    REQUIRE(newId);
    CHECK(oldId != newId);
    const ElementGeometry corner = elementGeometry(after.shape, ElementKind::Edge, *newId);
    CHECK(corner.centroid.x == doctest::Approx(10.0));
    CHECK(corner.centroid.y == doctest::Approx(6.0));
    const ElementGeometry stale = elementGeometry(after.shape, ElementKind::Edge, *oldId);
    CHECK_FALSE(sameGeometry(stale, corner));
}

TEST_CASE("geometric fingerprints") {
    const ElementGeometry a{"line", {1, 2, 3}, 4};
    CHECK(sameGeometry(a, a));
    CHECK(sameGeometry(a, {"line", {1, 2, 3 + 1e-9}, 4}));
    CHECK_FALSE(sameGeometry(a, {"circle", {1, 2, 3}, 4}));
    CHECK_FALSE(sameGeometry(a, {"line", {1, 2, 3.1}, 4}));
    CHECK_FALSE(sameGeometry(a, {"line", {1, 2, 3}, 4.1}));
}
