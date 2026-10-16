#include "facetry/render/pick.hpp"

#include "test_mesh.hpp"

#include <doctest.h>

using namespace facetry::render;

namespace {

// Looks at the unit cube's -Y face (face 3) head-on, so screen positions are easy to predict.
Camera frontCamera(const PickIndex& index) {
    Camera camera;
    camera.setViewport(400, 400);
    camera.setSceneBounds(index.bounds());
    camera.lookAt({0.5F, -4.0F, 0.5F}, {0.5F, 0.0F, 0.5F});
    return camera;
}

}  // namespace

TEST_CASE("ray cast hits the nearest face") {
    const PickIndex index(test::unitCube());
    const auto hit = index.raycast({{0.5F, -5.0F, 0.5F}, {0, 1, 0}});
    REQUIRE(hit.has_value());
    CHECK(hit->faceId == 3);
    CHECK(hit->distance == doctest::Approx(5.0F));
    CHECK_FALSE(index.raycast({{5, -5, 5}, {0, 1, 0}}).has_value());
}

TEST_CASE("pick prefers vertices, then edges, then faces") {
    const PickIndex index(test::unitCube());
    const Camera camera = frontCamera(index);

    const auto center = camera.project({0.5F, 0.0F, 0.5F});
    const auto face = index.pick(camera, center.x, center.y, 6);
    CHECK(face.kind == PickKind::Face);
    CHECK(face.id == 3);

    // Near the bottom edge of the front face (corners 0 and 1: edge 1).
    const auto bottom = camera.project({0.5F, 0.0F, 0.0F});
    const auto edge = index.pick(camera, bottom.x + 1, bottom.y - 4, 6);
    CHECK(edge.kind == PickKind::Edge);
    CHECK(edge.id == 1);

    // Same position with a small radius picks the face instead.
    CHECK(index.pick(camera, bottom.x + 1, bottom.y - 4, 2).kind == PickKind::Face);

    // Near corner 0 (vertex 1).
    const auto corner = camera.project({0.0F, 0.0F, 0.0F});
    const auto vertex = index.pick(camera, corner.x + 3, corner.y - 2, 6);
    CHECK(vertex.kind == PickKind::Vertex);
    CHECK(vertex.id == 1);

    // Far away from the cube: nothing.
    CHECK(index.pick(camera, 5, 5, 6).kind == PickKind::None);
}

TEST_CASE("hidden edges are not picked") {
    const PickIndex index(test::unitCube());
    Camera camera;
    camera.setViewport(400, 400);
    camera.setSceneBounds(index.bounds());
    // Slightly from above, so the back top edge (corners 6-7, edge 4) is visible and the back
    // bottom edge (corners 2-3, edge 2) is hidden behind the front face.
    camera.lookAt({0.5F, -4.0F, 1.5F}, {0.5F, 0.5F, 0.5F});

    const auto hiddenEdge = camera.project({0.5F, 1.0F, 0.0F});
    const auto picked = index.pick(camera, hiddenEdge.x, hiddenEdge.y, 3);
    CHECK_FALSE((picked.kind == PickKind::Edge && picked.id == 2));

    const auto visibleEdge = camera.project({0.5F, 1.0F, 1.0F});
    const auto top = index.pick(camera, visibleEdge.x, visibleEdge.y, 3);
    CHECK(top.kind == PickKind::Edge);
    CHECK(top.id == 4);
}

TEST_CASE("anchor falls back to the target plane") {
    const PickIndex index(test::unitCube());
    const Camera camera = frontCamera(index);
    const auto center = camera.project({0.5F, 0.0F, 0.5F});
    const Vec3 onModel = index.anchorAt(camera, center.x, center.y);
    CHECK(onModel.y == doctest::Approx(0.0F).epsilon(1e-4));
    const Vec3 offModel = index.anchorAt(camera, 5, 5);
    CHECK(offModel.y == doctest::Approx(0.0F).epsilon(1e-4));  // target plane y = 0
}
