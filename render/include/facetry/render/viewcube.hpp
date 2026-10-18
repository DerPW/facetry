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

// The arrows around the cube when it is seen straight on a face, as in Fusion (#63): four that
// turn the view a quarter turn to the next face on that side and, seen from the top or the
// bottom, two that roll it a quarter turn about the Z axis.
struct ViewCubeArrow {
    enum class Kind : std::uint32_t { Turn = 0, RollClockwise = 1, RollCounterClockwise = 2 };
    Kind kind = Kind::Turn;
    // Turn arrows: a triangle pointing at the cube, in points inside the square.
    std::array<float, 6> points{};
    // Where the arrow is; roll arrows are drawn as a curved arrow around it.
    std::array<float, 2> center{};
    // Where the view turns: the direction to look from and, for the top and the bottom, the side
    // the view leans toward (Camera::viewFrom).
    Vec3 direction;
    Vec3 lean;
};

[[nodiscard]] std::vector<ViewCubeArrow> viewCubeArrows(const Camera& camera, float size);
// Index in viewCubeArrows(camera, size) of the arrow under (x, y), or -1.
[[nodiscard]] int viewCubeArrowHit(const Camera& camera, float size, float x, float y);

// The compass ring around the cube's base: a closed polyline in points inside the square, and
// where its labels go: N (+Y), E (+X), S (-Y), W (-X). Dragging it turns the view around the Z
// axis only. Seen from the side the ring is a line, so there is none.
struct ViewCubeCompass {
    std::vector<float> ring;  // x0 y0 x1 y1 …; the last point repeats the first; empty if hidden
    std::array<std::array<float, 2>, 4> labels{};
};

[[nodiscard]] ViewCubeCompass viewCubeCompass(const Camera& camera, float size);
// Whether (x, y) is on the compass ring or one of its labels.
[[nodiscard]] bool viewCubeCompassHit(const Camera& camera, float size, float x, float y);

}  // namespace facetry::render
