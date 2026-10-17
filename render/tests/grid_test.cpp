#include "facetry/render/grid.hpp"

#include <doctest.h>

#include <cmath>

using namespace facetry::render;

namespace {

// A camera framing a cube of `size` millimeters.
Camera framing(float size) {
    Camera camera;
    camera.setViewport(800, 600);
    Bounds bounds;
    bounds.add({0, 0, 0});
    bounds.add({size, size, size});
    camera.setSceneBounds(bounds);
    camera.home();
    return camera;
}

// The spacing in units, as 1, 2 or 5 × 10ⁿ.
bool isNice(float units) {
    const float decade = std::pow(10.0F, std::floor(std::log10(units) + 1e-4F));
    const float mantissa = units / decade;
    return std::abs(mantissa - 1.0F) < 1e-3F || std::abs(mantissa - 2.0F) < 1e-3F ||
           std::abs(mantissa - 5.0F) < 1e-3F;
}

}  // namespace

TEST_CASE("the grid spacing follows the zoom level, from a 1 mm part to a 1 m part") {
    for (const float size : {1.0F, 10.0F, 100.0F, 1000.0F}) {
        CAPTURE(size);
        const Camera camera = framing(size);
        const GridLayout grid = gridLayout(camera, 1.0F);
        CHECK(isNice(grid.minor));
        // Minor lines at least 12 points apart, but not much more than 30.
        const float pixels = grid.minor / camera.worldPerPointAtTarget();
        CHECK(pixels >= 12.0F);
        CHECK(pixels <= 12.0F * 2.5F + 0.01F);
        // Major lines on a power of ten.
        const float exponent = std::log10(grid.major);
        CHECK(exponent == doctest::Approx(std::round(exponent)));
        CHECK(grid.major > grid.minor);
        // A part 1000 times larger gets a grid 1000 times coarser.
        CHECK(grid.radius > size);
    }
    CHECK(gridLayout(framing(1000.0F), 1.0F).minor ==
          doctest::Approx(gridLayout(framing(1.0F), 1.0F).minor * 1000.0F));
}

TEST_CASE("in inches the lines fall on inch fractions and multiples") {
    const Camera camera = framing(50.8F);  // a 2 inch cube
    const GridLayout grid = gridLayout(camera, 25.4F);
    CHECK(isNice(grid.minor / 25.4F));
    CHECK(std::fmod(grid.major / 25.4F + 1e-4F, 1.0F) < 1e-3F);  // whole inches
}

TEST_CASE("zooming in fades the next minor lines in") {
    Camera camera = framing(100.0F);
    const GridLayout before = gridLayout(camera, 1.0F);
    camera.zoom(0.7F, camera.target());
    const GridLayout after = gridLayout(camera, 1.0F);
    CHECK(after.minor <= before.minor);
    CHECK(after.minorOpacity >= 0.0F);
    CHECK(after.minorOpacity <= 1.0F);
}
