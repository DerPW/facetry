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

    // In wireframe or through transparent faces the hidden edge can be picked.
    const auto throughFaces = index.pick(camera, hiddenEdge.x, hiddenEdge.y, 3, true);
    CHECK(throughFaces.kind == PickKind::Edge);
    CHECK(throughFaces.id == 2);
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

TEST_CASE("sketch regions are picked over the model") {
    const PickIndex model(test::unitCube());
    const Camera camera = frontCamera(model);
    // Two regions inside the cube, behind its front face: x 0.1–0.4 and 0.6–0.9, z 0.1–0.9.
    RenderMesh mesh;
    for (std::uint32_t r = 0; r < 2; ++r) {
        const float x0 = r == 0 ? 0.1F : 0.6F;
        const float x1 = x0 + 0.3F;
        const auto base = static_cast<std::uint32_t>(mesh.positions.size() / 3);
        mesh.positions.insert(mesh.positions.end(),
                              {x0, 0.5F, 0.1F, x1, 0.5F, 0.1F, x1, 0.5F, 0.9F, x0, 0.5F, 0.9F});
        mesh.normals.insert(mesh.normals.end(), {0, -1, 0, 0, -1, 0, 0, -1, 0, 0, -1, 0});
        mesh.triangles.insert(mesh.triangles.end(),
                              {base, base + 1, base + 2, base, base + 2, base + 3});
        mesh.triangleFaceIds.insert(mesh.triangleFaceIds.end(), {r + 1, r + 1});
    }
    const PickIndex regions(mesh);
    const auto left = camera.project({0.25F, 0.0F, 0.5F});
    const auto right = camera.project({0.75F, 0.0F, 0.5F});
    const auto between = camera.project({0.5F, 0.0F, 0.5F});

    const auto pick = [&](const Camera::Projected& at, PickFilter filter) {
        return pickWithRegions(model, regions, camera, at.x, at.y, 6, false, filter);
    };
    CHECK(pick(left, PickFilter::Everything).kind == PickKind::Region);
    CHECK(pick(left, PickFilter::Everything).id == 1);
    CHECK(pick(right, PickFilter::Regions).id == 2);
    CHECK(pick(between, PickFilter::Everything).kind == PickKind::Face);
    CHECK(pick(between, PickFilter::Regions).kind == PickKind::None);
    CHECK(pick(left, PickFilter::Faces).kind == PickKind::Face);

    // A click's candidates: the region, then the model's elements behind it.
    const auto candidates = candidatesWithRegions(model, regions, camera, left.x, left.y, 6, false,
                                                  PickFilter::Everything);
    REQUIRE(candidates.size() >= 2);
    CHECK(candidates[0].kind == PickKind::Region);
    CHECK(candidates[1].kind == PickKind::Face);
    CHECK(
        candidatesWithRegions(model, regions, camera, left.x, left.y, 6, false, PickFilter::Regions)
            .size() == 1);

    // Without regions, picking is the model's.
    CHECK(pickWithRegions(model, PickIndex(), camera, left.x, left.y, 6, false,
                          PickFilter::Everything)
              .kind == PickKind::Face);
}
