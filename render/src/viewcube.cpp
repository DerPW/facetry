#include "facetry/render/viewcube.hpp"

#include <algorithm>

namespace facetry::render {
namespace {

struct FaceFrame {
    CubeFace face;
    Vec3 normal;
    // In-plane axes with cross(u, v) == normal, so cells come out counter-clockwise from
    // outside. On the side faces v points up, so labels stand upright.
    Vec3 u;
    Vec3 v;
};

constexpr std::array<FaceFrame, 6> kFaces{{
    {CubeFace::Right, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}},
    {CubeFace::Left, {-1, 0, 0}, {0, -1, 0}, {0, 0, 1}},
    {CubeFace::Back, {0, 1, 0}, {-1, 0, 0}, {0, 0, 1}},
    {CubeFace::Front, {0, -1, 0}, {1, 0, 0}, {0, 0, 1}},
    {CubeFace::Top, {0, 0, 1}, {1, 0, 0}, {0, 1, 0}},
    {CubeFace::Bottom, {0, 0, -1}, {1, 0, 0}, {0, -1, 0}},
}};

// Cell boundaries along each face axis: the edge and corner bands are 20 % of the face.
constexpr std::array<float, 4> kSteps{-1.0F, -0.6F, 0.6F, 1.0F};

// Half the cube's width as a share of the square: the cube fits at every orientation.
constexpr float kScale = 0.28F;

bool contains(const ViewCubeCell& cell, float x, float y) {
    for (std::size_t i = 0; i < 4; ++i) {
        const std::size_t j = (i + 1) % 4;
        const float ax = cell.points[i * 2];
        const float ay = cell.points[i * 2 + 1];
        const float bx = cell.points[j * 2];
        const float by = cell.points[j * 2 + 1];
        // Screen y points down, so counter-clockwise corners have the inside on the right.
        if ((bx - ax) * (y - ay) - (by - ay) * (x - ax) > 0.0F) {
            return false;
        }
    }
    return true;
}

}  // namespace

std::vector<ViewCubeCell> viewCubeCells(const Camera& camera, float size) {
    const Vec3 right = camera.right();
    const Vec3 up = camera.up();
    const Vec3 toViewer = camera.forward() * -1.0F;
    const float center = size * 0.5F;
    const float scale = size * kScale;

    std::vector<ViewCubeCell> cells;
    for (const FaceFrame& frame : kFaces) {
        if (dot(frame.normal, toViewer) <= 0.01F) {
            continue;  // turned away, or seen edge-on
        }
        for (std::size_t i = 0; i < 3; ++i) {
            for (std::size_t j = 0; j < 3; ++j) {
                ViewCubeCell cell;
                cell.face = frame.face;
                const std::array<Vec3, 4> corners{
                    frame.normal + frame.u * kSteps[i] + frame.v * kSteps[j],
                    frame.normal + frame.u * kSteps[i + 1] + frame.v * kSteps[j],
                    frame.normal + frame.u * kSteps[i + 1] + frame.v * kSteps[j + 1],
                    frame.normal + frame.u * kSteps[i] + frame.v * kSteps[j + 1],
                };
                Vec3 middle;
                for (std::size_t k = 0; k < 4; ++k) {
                    cell.points[k * 2] = center + dot(corners[k], right) * scale;
                    cell.points[k * 2 + 1] = center - dot(corners[k], up) * scale;
                    middle = middle + corners[k] * 0.25F;
                }
                cell.depth = dot(middle, toViewer);
                cell.direction = frame.normal + frame.u * (static_cast<float>(i) - 1.0F) +
                                 frame.v * (static_cast<float>(j) - 1.0F);
                cell.region = static_cast<std::uint32_t>((i != 1 ? 1 : 0) + (j != 1 ? 1 : 0));
                cells.push_back(cell);
            }
        }
    }
    std::stable_sort(cells.begin(), cells.end(), [](const ViewCubeCell& a, const ViewCubeCell& b) {
        return a.depth < b.depth;
    });
    return cells;
}

int viewCubeHit(const Camera& camera, float size, float x, float y) {
    const std::vector<ViewCubeCell> cells = viewCubeCells(camera, size);
    for (std::size_t i = cells.size(); i-- > 0;) {
        if (contains(cells[i], x, y)) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

}  // namespace facetry::render
