// Named views, the home view, animation and the ViewCube.
#include "facetry/render/viewcube.hpp"

#include "facetry/render/pick.hpp"

#include "test_mesh.hpp"

#include <doctest.h>

#include <cmath>

using namespace facetry::render;

namespace {

Camera sceneCamera() {
    Camera camera;
    camera.setViewport(800, 600);
    Bounds bounds;
    bounds.add({0, 0, 0});
    bounds.add({20, 10, 5});
    camera.setSceneBounds(bounds);
    camera.home();
    return camera;
}

void checkVector(Vec3 actual, Vec3 expected) {
    CHECK(actual.x == doctest::Approx(expected.x).epsilon(1e-2));
    CHECK(actual.y == doctest::Approx(expected.y).epsilon(1e-2));
    CHECK(actual.z == doctest::Approx(expected.z).epsilon(1e-2));
}

// The cell under the center of the cell that turns the view to `direction`.
const ViewCubeCell* cellTo(const std::vector<ViewCubeCell>& cells, Vec3 direction) {
    for (const ViewCubeCell& cell : cells) {
        if (length(cell.direction - direction) < 1e-6F) {
            return &cell;
        }
    }
    return nullptr;
}

}  // namespace

TEST_CASE("named views look along the axes, with the front down in the top view") {
    Camera camera = sceneCamera();
    const float distance = length(camera.eye() - camera.target());

    camera.viewFrom({0, 0, 1});
    checkVector(camera.forward(), {0, 0, -1});
    checkVector(camera.right(), {1, 0, 0});
    checkVector(camera.up(), {0, 1, 0});
    CHECK(length(camera.eye() - camera.target()) == doctest::Approx(distance));

    camera.viewFrom({0, -1, 0});  // front
    checkVector(camera.forward(), {0, 1, 0});
    checkVector(camera.right(), {1, 0, 0});
    checkVector(camera.up(), {0, 0, 1});

    camera.viewFrom({1, 0, 0});  // right
    checkVector(camera.right(), {0, 1, 0});
}

TEST_CASE("orbiting away from a view straight down works") {
    Camera camera = sceneCamera();
    camera.viewFrom({0, 0, 1});
    const float before = std::abs(camera.forward().z);
    camera.orbit(0, -40, camera.target());  // tilt toward the horizon
    CHECK(std::abs(camera.forward().z) < before);
    camera.orbit(80, 0, camera.target());  // and turn around Z
    CHECK(std::abs(camera.forward().z) < before);
}

TEST_CASE("view changes animate along the sphere and stop at any other move") {
    Camera camera = sceneCamera();
    camera.viewFrom({0, -1, 0});
    const Vec3 target = camera.target();
    const float distance = length(camera.eye() - target);

    camera.viewFrom({0, 1, 0}, 0.3F);  // front to back: opposite directions
    CHECK(camera.animating());
    CHECK(camera.advance(0.15F));
    // Halfway the camera is beside the model, not inside it.
    CHECK(length(camera.eye() - target) == doctest::Approx(distance));
    CHECK(std::abs(camera.forward().y) < 0.2F);
    CHECK_FALSE(camera.advance(0.2F));
    CHECK_FALSE(camera.animating());
    checkVector(camera.forward(), {0, -1, 0});

    camera.viewFrom({0, 0, 1}, 0.3F);
    camera.advance(0.1F);
    camera.pan(5, 0);
    CHECK_FALSE(camera.animating());
}

TEST_CASE("home looks from the front right above; fit keeps the direction") {
    Camera camera = sceneCamera();
    checkVector(camera.forward() * -1.0F, normalize(Vec3{1, -1, 0.8F}));
    camera.viewFrom({0, -1, 0});
    camera.zoom(0.2F, camera.target());
    camera.fit();
    checkVector(camera.forward(), {0, 1, 0});
    const auto center = camera.project({10, 5, 2.5F});
    CHECK(center.x == doctest::Approx(400).epsilon(1e-3));
    CHECK(center.y == doctest::Approx(300).epsilon(1e-3));

    camera.home(0.3F);
    camera.advance(1.0F);
    checkVector(camera.forward() * -1.0F, normalize(Vec3{1, -1, 0.8F}));
}

TEST_CASE("the ViewCube shows the faces turned toward the viewer") {
    Camera camera = sceneCamera();
    camera.viewFrom({0, -1, 0});
    const auto front = viewCubeCells(camera, 100);
    REQUIRE(front.size() == 9);  // only the front face
    const ViewCubeCell* center = cellTo(front, {0, -1, 0});
    REQUIRE(center != nullptr);
    CHECK(center->face == CubeFace::Front);
    CHECK(center->region == 0);
    CHECK(viewCubeHit(camera, 100, 50, 50) == static_cast<int>(center - front.data()));
    // Just inside the top of the front face: the top-front edge.
    const int edge = viewCubeHit(camera, 100, 50, 50 - 100 * 0.24F + 2);
    REQUIRE(edge >= 0);
    checkVector(front[static_cast<std::size_t>(edge)].direction, {0, -1, 1});
    CHECK(front[static_cast<std::size_t>(edge)].region == 1);
    CHECK(viewCubeHit(camera, 100, 2, 2) == -1);

    // From the home view: front, right and top, and the corner between them in the middle.
    camera.home();
    const auto home = viewCubeCells(camera, 100);
    CHECK(home.size() == 27);
    for (std::size_t i = 1; i < home.size(); ++i) {
        CHECK(home[i - 1].depth <= home[i].depth);
    }
    const int corner = viewCubeHit(camera, 100, 50, 50);
    REQUIRE(corner >= 0);
    checkVector(home[static_cast<std::size_t>(corner)].direction, {1, -1, 1});
    CHECK(home[static_cast<std::size_t>(corner)].region == 2);
}

TEST_CASE("turning from a view straight down to a side never flips the picture") {
    Camera camera = sceneCamera();
    camera.viewFrom({0, 0, 1});
    camera.viewFrom({0, 1, 0}, 0.3F);  // top to back: a sphere's shortest path crosses the pole
    Vec3 up = camera.up();
    while (camera.advance(0.01F)) {
        CHECK(dot(camera.up(), up) > 0.9F);
        up = camera.up();
    }
    checkVector(camera.forward(), {0, -1, 0});
}

TEST_CASE("the ViewCube's arrows turn a view straight on a face to the next face") {
    Camera camera = sceneCamera();
    CHECK(viewCubeArrows(camera, 100).empty());  // the home view is not straight on a face

    camera.viewFrom({0, -1, 0});  // front
    const auto front = viewCubeArrows(camera, 100);
    REQUIRE(front.size() == 4);  // no roll arrows from the side
    for (std::size_t i = 0; i < front.size(); ++i) {
        CHECK(front[i].kind == ViewCubeArrow::Kind::Turn);
        CHECK(viewCubeArrowHit(camera, 100, front[i].center[0], front[i].center[1]) ==
              static_cast<int>(i));
    }
    CHECK(viewCubeArrowHit(camera, 100, 50, 50) == -1);
    // The arrow above the cube turns to the top, with the front at the bottom of the screen;
    // the one below to the bottom, with the front at the top.
    const ViewCubeArrow& above = front[0];
    CHECK(above.center[1] < 10.0F);
    checkVector(above.direction, {0, 0, 1});
    camera.viewFrom(above.direction, 0.0F, above.lean);
    checkVector(camera.up(), {0, 1, 0});
    camera.viewFrom({0, -1, 0});
    const ViewCubeArrow& below = front[1];
    checkVector(below.direction, {0, 0, -1});
    camera.viewFrom(below.direction, 0.0F, below.lean);
    checkVector(camera.up(), {0, -1, 0});
    camera.viewFrom({0, -1, 0});
    checkVector(front[2].direction, {1, 0, 0});  // right of the cube: the right side
}

TEST_CASE("the ViewCube's roll arrows roll views from the top and the bottom") {
    for (const float z : {1.0F, -1.0F}) {
        CAPTURE(z);
        Camera camera = sceneCamera();
        camera.viewFrom({0, 0, z});
        const auto arrows = viewCubeArrows(camera, 100);
        REQUIRE(arrows.size() == 6);
        const ViewCubeArrow& clockwise = arrows[4];
        CHECK(clockwise.kind == ViewCubeArrow::Kind::RollClockwise);
        CHECK(arrows[5].kind == ViewCubeArrow::Kind::RollCounterClockwise);
        // Clockwise, the side on the right comes to the bottom of the screen.
        const Vec3 right = camera.right();
        const Vec3 up = camera.up();
        camera.viewFrom(clockwise.direction, 0.0F, clockwise.lean);
        checkVector(camera.forward(), {0, 0, -z});
        checkVector(camera.up(), right * -1.0F);
        // Counter-clockwise from there rolls back; once more brings that side to the top.
        const auto rollBack = [&camera] {
            const auto rolled = viewCubeArrows(camera, 100);
            REQUIRE(rolled.size() == 6);
            camera.viewFrom(rolled[5].direction, 0.0F, rolled[5].lean);
        };
        rollBack();
        checkVector(camera.forward(), {0, 0, -z});
        checkVector(camera.up(), up);
        checkVector(camera.right(), right);
        rollBack();
        checkVector(camera.up(), right);
    }
}

TEST_CASE("the ViewCube's compass rings the cube's base when seen from above or below") {
    Camera camera = sceneCamera();
    const ViewCubeCompass home = viewCubeCompass(camera, 100);
    REQUIRE(home.ring.size() == 49 * 2);
    CHECK(home.ring[0] == doctest::Approx(home.ring[home.ring.size() - 2]));
    // From the front right above: north (+Y) is farther up the screen than south, east right of
    // west.
    const auto& [north, east, south, west] = home.labels;
    CHECK(north[1] < south[1]);
    CHECK(east[0] > west[0]);
    CHECK(viewCubeCompassHit(camera, 100, north[0], north[1]));
    CHECK(viewCubeCompassHit(camera, 100, home.ring[10], home.ring[11]));
    CHECK_FALSE(viewCubeCompassHit(camera, 100, 2, 2));

    camera.viewFrom({0, 0, 1});
    const ViewCubeCompass top = viewCubeCompass(camera, 100);
    REQUIRE_FALSE(top.ring.empty());
    CHECK(top.labels[0][0] == doctest::Approx(50).epsilon(1e-2));  // north straight up
    CHECK(top.labels[0][1] < 20.0F);

    camera.viewFrom({0, -1, 0});  // from the side the ring would be a line
    CHECK(viewCubeCompass(camera, 100).ring.empty());
    CHECK_FALSE(viewCubeCompassHit(camera, 100, 50, 74));
}

TEST_CASE("element bounds for fitting the selection") {
    const PickIndex index(test::unitCube(2.0F));
    const Bounds top = index.elementBounds(PickKind::Face, 6);  // +Z
    CHECK(top.min.z == doctest::Approx(2));
    CHECK(top.max.z == doctest::Approx(2));
    CHECK(top.max.x == doctest::Approx(2));
    CHECK(index.elementBounds(PickKind::Edge, 1).radius() > 0.0F);
    CHECK_FALSE(index.elementBounds(PickKind::Vertex, 1).isEmpty());
    CHECK(index.elementBounds(PickKind::Face, 99).isEmpty());
    CHECK(index.elementBounds(PickKind::None, 0).isEmpty());
}
