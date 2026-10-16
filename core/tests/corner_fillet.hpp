// The M0.6 prototype model: a rectangle sketch (width × depth), extruded by height, with the
// vertical edge at the corner (width, depth) rounded. Shared by the document and naming tests.
#pragma once

#include "facetry/core/document.hpp"
#include "facetry/core/naming.hpp"
#include "facetry/core/regeneration.hpp"
#include "facetry/core/session.hpp"

#include <doctest.h>

#include <cmath>
#include <numbers>
#include <string>

namespace facetry::test {

using namespace facetry::core;

inline constexpr double kWidth = 10.0;
inline constexpr double kDepth = 6.0;
inline constexpr double kHeight = 4.0;
inline constexpr double kRadius = 1.5;

// The fillet's ID. newId() continues after the highest number in use (p4, l4).
inline const std::string kFillet = "f5";

inline Feature feature(std::string id, std::string name, FeatureData data) {
    Feature result;
    result.id = std::move(id);
    result.name = std::move(name);
    result.data = std::move(data);
    return result;
}

// Sketch and extrude: the rectangle's corners are p1 (0, 0), p2 (width, 0), p3 (width, depth)
// and p4 (0, depth); line l2 runs from p2 to p3, l3 from p3 to p4.
inline Document rectangleBlock() {
    Document document;
    document.parameters = {
        {"width", kWidth}, {"depth", kDepth}, {"height", kHeight}, {"radius", kRadius}};
    Sketch sketch;
    sketch.points = {{"p1", 0, 0},
                     {"p2", Value::of("width"), 0},
                     {"p3", Value::of("width"), Value::of("depth")},
                     {"p4", 0, Value::of("depth")}};
    sketch.lines = {{"l1", "p1", "p2"}, {"l2", "p2", "p3"}, {"l3", "p3", "p4"}, {"l4", "p4", "p1"}};
    document.root.timeline.push_back(feature("f1", "Sketch 1", sketch));
    document.root.timeline.push_back(
        feature("f2", "Extrude 1", Extrude{.profile = "f1", .distance = Value::of("height")}));
    return document;
}

// The vertical edge standing at (x, y), found the way a user would pick it: by where it is.
inline std::uint32_t edgeAt(const NamedShape& body, double x, double y) {
    for (std::uint32_t id = 1; id <= body.edges.size(); ++id) {
        const ElementGeometry edge = elementGeometry(body.shape, ElementKind::Edge, id);
        if (edge.type == "line" && std::abs(edge.centroid.x - x) < 1e-9 &&
            std::abs(edge.centroid.y - y) < 1e-9) {
            return id;
        }
    }
    FAIL("no vertical edge at (" << x << ", " << y << ")");
    return 0;
}

// A session with the rectangle block, regenerated, and a fillet on the edge at the corner
// (width, depth) added as a user would: select the edge, then create the fillet.
inline Session cornerFilletSession() {
    Session session(rectangleBlock());
    const Regeneration& block = session.regenerate();
    const auto& body = *block.model.bodies.at("f2");
    const Reference corner =
        makeReference(block.model, "f2", ElementKind::Edge, edgeAt(body, kWidth, kDepth));
    session.edit([&](Document& document) {
        const std::string id = document.newId("f");
        CHECK(id == kFillet);
        document.root.timeline.push_back(
            feature(id, "Fillet 1", Fillet{.edges = {corner}, .radius = Value::of("radius")}));
    });
    return session;
}

// The round of the last feature, a fillet: exactly one face named "<fillet>:fillet[…]".
inline std::uint32_t roundFace(const NamedShape& body, const std::string& fillet) {
    std::uint32_t found = 0;
    for (std::uint32_t id = 1; id <= body.faces.size(); ++id) {
        if (body.faces[id - 1].starts_with(fillet + ":fillet[")) {
            CHECK_MESSAGE(found == 0, "more than one round");
            found = id;
        }
    }
    return found;
}

// Checks that the fillet rounds the vertical edge at the corner (x, y) of a block of height
// `height`, and that the body has the expected volume.
inline void expectRoundAt(const Regeneration& regeneration, const std::string& fillet, double x,
                          double y, double height, double radius, double blockVolume) {
    const FeatureResult* result = regeneration.find(fillet);
    REQUIRE(result != nullptr);
    CHECK(result->status == FeatureStatus::Ok);
    const NamedShape& body = *regeneration.model.bodies.at("f2");
    const std::uint32_t round = roundFace(body, fillet);
    REQUIRE(round != 0);
    const ElementGeometry geometry = elementGeometry(body.shape, ElementKind::Face, round);
    CHECK(geometry.type == "cylinder");
    // A quarter cylinder around (x - r, y - r): its centroid lies 2r/π further out on x and y.
    const double offset = radius - 2.0 * radius / std::numbers::pi;
    CHECK(geometry.centroid.x == doctest::Approx(x - offset));
    CHECK(geometry.centroid.y == doctest::Approx(y - offset));
    CHECK(geometry.centroid.z == doctest::Approx(height / 2.0));
    const double removed = (1.0 - std::numbers::pi / 4.0) * radius * radius * height;
    CHECK(body.shape.properties().volume == doctest::Approx(blockVolume - removed));
}

inline void setParameter(Session& session, const std::string& name, double value) {
    session.edit([&](Document& document) {
        for (Parameter& parameter : document.parameters) {
            if (parameter.name == name) {
                parameter.value = value;
            }
        }
    });
}

}  // namespace facetry::test
