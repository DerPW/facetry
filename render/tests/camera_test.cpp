#include "facetry/render/camera.hpp"

#include <doctest.h>

#include <cmath>

using namespace facetry::render;

namespace {

Camera makeCamera(Camera::Projection projection = Camera::Projection::Perspective) {
    Camera camera;
    camera.setViewport(800, 600);
    Bounds bounds;
    bounds.add({0, 0, 0});
    bounds.add({20, 10, 5});
    camera.setSceneBounds(bounds);
    camera.fit();
    camera.setProjection(projection);
    return camera;
}

void checkScreen(const Camera& camera, Vec3 world, float x, float y) {
    const auto p = camera.project(world);
    CHECK(p.w > 0.0F);
    CHECK(p.x == doctest::Approx(x).epsilon(1e-3));
    CHECK(p.y == doctest::Approx(y).epsilon(1e-3));
}

}  // namespace

TEST_CASE("fit centers the scene") {
    const Camera camera = makeCamera();
    checkScreen(camera, {10, 5, 2.5F}, 400, 300);
}

TEST_CASE("rays and projection agree") {
    for (const auto projection :
         {Camera::Projection::Perspective, Camera::Projection::Orthographic}) {
        const Camera camera = makeCamera(projection);
        for (const auto [x, y] : {std::pair{400.0F, 300.0F}, {50.0F, 70.0F}, {750.0F, 560.0F}}) {
            const Ray ray = camera.rayAt(x, y);
            checkScreen(camera, ray.origin + ray.direction * 37.0F, x, y);
        }
    }
}

TEST_CASE("orbit keeps the distance to the pivot and never flips") {
    Camera camera = makeCamera();
    const Vec3 pivot{20, 10, 5};
    const float before = length(camera.eye() - pivot);
    camera.orbit(120, -45, pivot);
    CHECK(length(camera.eye() - pivot) == doctest::Approx(before).epsilon(1e-4));
    for (int i = 0; i < 50; ++i) {
        camera.orbit(0, 100, pivot);
        CHECK(std::abs(camera.forward().z) < 0.9951F);
    }
}

TEST_CASE("zoom keeps the anchor under the pointer") {
    for (const auto projection :
         {Camera::Projection::Perspective, Camera::Projection::Orthographic}) {
        Camera camera = makeCamera(projection);
        const Vec3 anchor = camera.pointOnTargetPlane(600, 150);
        camera.zoom(0.5F, anchor);
        checkScreen(camera, anchor, 600, 150);
        camera.zoom(1.7F, anchor);
        checkScreen(camera, anchor, 600, 150);
    }
}

TEST_CASE("pan moves the scene with the pointer") {
    for (const auto projection :
         {Camera::Projection::Perspective, Camera::Projection::Orthographic}) {
        Camera camera = makeCamera(projection);
        const Vec3 point = camera.pointOnTargetPlane(300, 200);
        camera.pan(40, -25);
        checkScreen(camera, point, 340, 175);
    }
}

TEST_CASE("depth range follows the backend convention") {
    const Camera camera = makeCamera();
    const Vec3 center{10, 5, 2.5F};
    const Vec4 gl = transform(camera.viewProjection(false), center);
    const Vec4 metal = transform(camera.viewProjection(true), center);
    CHECK(gl.z / gl.w > -1.0F);
    CHECK(gl.z / gl.w < 1.0F);
    CHECK(metal.z / metal.w > 0.0F);
    CHECK(metal.z / metal.w < 1.0F);
}
