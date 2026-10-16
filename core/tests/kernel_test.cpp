#include "facetry/core/error.hpp"
#include "facetry/core/mesh.hpp"
#include "facetry/core/modeling.hpp"
#include "facetry/core/stl.hpp"

#include <doctest.h>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <set>
#include <sstream>

using namespace facetry::core;

TEST_CASE("straight edges parallel to an axis") {
    const Shape box = makeBox(20, 10, 5);
    CHECK(straightEdgesParallelTo(box, {0, 0, 1}).size() == 4);
    CHECK(straightEdgesParallelTo(box, {1, 0, 0}).size() == 4);
    CHECK(straightEdgesParallelTo(box, {1, 1, 0}).empty());
    CHECK_THROWS_AS(static_cast<void>(straightEdgesParallelTo(box, {0, 0, 0})), Error);
}

TEST_CASE("fillet on all vertical edges matches the analytic volume") {
    const Shape box = makeBox(20, 10, 5);
    const auto edges = straightEdgesParallelTo(box, {0, 0, 1});
    const Shape rounded = fillet(box, edges, 2.0);
    const double removed = 4.0 * (1.0 - std::numbers::pi / 4.0) * 4.0 * 5.0;
    CHECK(rounded.properties().volume == doctest::Approx(1000.0 - removed).epsilon(1e-9));
}

TEST_CASE("invalid input raises facetry errors, not kernel exceptions") {
    const Shape box = makeBox(1, 1, 1);
    const auto expectCode = [](auto&& action, ErrorCode code) {
        try {
            action();
            FAIL("expected an error");
        } catch (const Error& error) {
            CHECK(error.code() == code);
        }
    };
    expectCode([&] { static_cast<void>(makeBox(0, 1, 1)); }, ErrorCode::InvalidArgument);
    expectCode([&] { static_cast<void>(makeCylinder(0, 0, -1, 1)); }, ErrorCode::InvalidArgument);
    const std::uint32_t edge = 1;
    expectCode([&] { static_cast<void>(fillet(box, {&edge, 1}, 5.0)); }, ErrorCode::Kernel);
    expectCode([&] { static_cast<void>(fillet(Shape{}, {&edge, 1}, 0.1)); },
               ErrorCode::InvalidArgument);
}

TEST_CASE("tessellation produces consistent buffers") {
    const Shape shape = booleanOp(BooleanOp::Cut, makeBox(20, 10, 5), makeCylinder(10, 5, 2, 5));
    const Mesh mesh = tessellate(shape, {.linearDeflection = 0.05, .angularDeflection = 0.2});
    const TopologyCounts counts = shape.topologyCounts();

    REQUIRE(mesh.triangleCount() > 0);
    CHECK(mesh.normals.size() == mesh.positions.size());
    CHECK(mesh.triangleFaceIds.size() == mesh.triangleCount());
    CHECK(mesh.vertexIds.size() == counts.vertices);
    CHECK(mesh.edgeOffsets.size() == mesh.edgeIds.size() + 1);
    CHECK(std::size_t{mesh.edgeOffsets.back()} * 3 == mesh.edgePoints.size());

    // Every face is triangulated.
    const std::set<std::uint32_t> faces(mesh.triangleFaceIds.begin(), mesh.triangleFaceIds.end());
    CHECK(faces.size() == counts.faces);

    // Normals are unit length.
    for (std::size_t i = 0; i < mesh.normals.size(); i += 3) {
        const double length = std::sqrt(double{mesh.normals[i]} * mesh.normals[i] +
                                        double{mesh.normals[i + 1]} * mesh.normals[i + 1] +
                                        double{mesh.normals[i + 2]} * mesh.normals[i + 2]);
        REQUIRE(length == doctest::Approx(1.0).epsilon(1e-4));
    }

    // Winding is outward: the signed volume of the closed mesh equals the solid's volume.
    double signedVolume = 0.0;
    const auto p = [&](std::uint32_t index, int axis) {
        return double{mesh.positions[std::size_t{index} * 3 + static_cast<std::size_t>(axis)]};
    };
    for (std::size_t t = 0; t < mesh.triangleCount(); ++t) {
        const std::uint32_t a = mesh.triangles[t * 3];
        const std::uint32_t b = mesh.triangles[t * 3 + 1];
        const std::uint32_t c = mesh.triangles[t * 3 + 2];
        signedVolume += (p(a, 0) * (p(b, 1) * p(c, 2) - p(b, 2) * p(c, 1)) -
                         p(a, 1) * (p(b, 0) * p(c, 2) - p(b, 2) * p(c, 0)) +
                         p(a, 2) * (p(b, 0) * p(c, 1) - p(b, 1) * p(c, 0))) /
                        6.0;
    }
    CHECK(signedVolume == doctest::Approx(shape.properties().volume).epsilon(1e-2));
}

TEST_CASE("binary STL has the expected size") {
    const Mesh mesh = tessellate(makeBox(1, 2, 3));
    std::ostringstream out;
    writeBinaryStl(mesh, out);
    CHECK(mesh.triangleCount() == 12);
    CHECK(out.str().size() == 84 + 50 * mesh.triangleCount());
}
