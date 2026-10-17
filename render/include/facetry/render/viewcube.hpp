#pragma once

// The ViewCube: a small cube that shows the camera's orientation and turns the view to a face,
// edge or corner when clicked. The shells draw the cells and forward clicks; the geometry and
// the hit test are shared.

#include "facetry/render/camera.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace facetry::render {

// The cube's faces, in the order of ViewCubeCell::face.
enum class CubeFace : std::uint32_t {
    Right = 0,
    Left = 1,
    Back = 2,
    Front = 3,
    Top = 4,
    Bottom = 5
};

struct ViewCubeCell {
    // The corners on the screen, in points inside the cube's square (origin top-left),
    // counter-clockwise as seen by the viewer.
    std::array<float, 8> points{};
    // Where a click on the cell looks from: the outward normal of a face, or the sum of two
    // (an edge) or three (a corner).
    Vec3 direction;
    // Larger is nearer to the viewer.
    float depth = 0.0F;
    CubeFace face = CubeFace::Right;
    // 0 the face's center, 1 an edge, 2 a corner.
    std::uint32_t region = 0;
};

// Each visible face is split into a 3 × 3 grid of cells: the center, four edges and four
// corners. Returns the cells of the faces turned toward the viewer, far to near, for a cube
// drawn in a square of `size` points.
[[nodiscard]] std::vector<ViewCubeCell> viewCubeCells(const Camera& camera, float size);

// Index in viewCubeCells(camera, size) of the cell under (x, y), or -1.
[[nodiscard]] int viewCubeHit(const Camera& camera, float size, float x, float y);

}  // namespace facetry::render
