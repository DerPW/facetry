#include "facetry/render/viewcube.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>

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

// Half the cube's width as a share of the square: the cube fits at every orientation and leaves
// room around it for the arrows and the compass.
constexpr float kScale = 0.24F;
// The compass ring's radius around the cube's base, in cube units.
constexpr float kCompassRadius = 1.5F;

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

namespace {

// The axis closest to `v`, as a unit vector.
Vec3 snapToAxis(Vec3 v) {
    const float ax = std::abs(v.x);
    const float ay = std::abs(v.y);
    const float az = std::abs(v.z);
    if (ax >= ay && ax >= az) {
        return {v.x > 0.0F ? 1.0F : -1.0F, 0.0F, 0.0F};
    }
    if (ay >= az) {
        return {0.0F, v.y > 0.0F ? 1.0F : -1.0F, 0.0F};
    }
    return {0.0F, 0.0F, v.z > 0.0F ? 1.0F : -1.0F};
}

// Arrows are hit within this share of the square around their center; the compass within this
// share of the ring or a label.
constexpr float kArrowReach = 0.08F;
constexpr float kCompassReach = 0.05F;
// The compass shows when the view looks down or up at least this much (|forward.z|).
constexpr float kCompassMinTilt = 0.2F;

}  // namespace

std::vector<ViewCubeArrow> viewCubeArrows(const Camera& camera, float size) {
    const Vec3 toViewer = camera.forward() * -1.0F;
    const Vec3 face = snapToAxis(toViewer);
    if (dot(face, toViewer) < 0.9995F) {
        return {};  // not straight on a face
    }
    const float c = size * 0.5F;
    const Vec3 up = snapToAxis(camera.up());
    const Vec3 right = snapToAxis(camera.right());
    std::vector<ViewCubeArrow> arrows;
    // On each side, a triangle pointing at the cube, from 0.49 to 0.42 of the square out from the
    // center.
    const std::array<std::pair<Vec3, std::array<float, 2>>, 4> sides{{
        {up, {0.0F, -1.0F}},
        {up * -1.0F, {0.0F, 1.0F}},
        {right, {1.0F, 0.0F}},
        {right * -1.0F, {-1.0F, 0.0F}},
    }};
    for (const auto& [direction, screen] : sides) {
        ViewCubeArrow arrow;
        const float tip = 0.42F * size;
        const float base = 0.49F * size;
        const float half = 0.06F * size;
        const float px = -screen[1];  // across the arrow
        const float py = screen[0];
        arrow.points = {c + screen[0] * tip,
                        c + screen[1] * tip,
                        c + screen[0] * base + px * half,
                        c + screen[1] * base + py * half,
                        c + screen[0] * base - px * half,
                        c + screen[1] * base - py * half};
        arrow.center = {c + screen[0] * (tip + base) * 0.5F, c + screen[1] * (tip + base) * 0.5F};
        arrow.direction = direction;
        // Tilting to the top or the bottom leans toward the face left behind, as turning the cube
        // over that edge would.
        arrow.lean = face;
        arrows.push_back(arrow);
    }
    if (std::abs(face.z) > 0.5F) {
        // From the top or the bottom: roll a quarter turn, so that the side on the right
        // (clockwise) or on the left comes to the bottom of the screen. The view leans toward the
        // side at the bottom from above and toward the one at the top from below.
        for (const bool clockwise : {true, false}) {
            ViewCubeArrow arrow;
            arrow.kind = clockwise ? ViewCubeArrow::Kind::RollClockwise
                                   : ViewCubeArrow::Kind::RollCounterClockwise;
            // Both at the top right, as in Fusion, around the cube's corner: clockwise below.
            arrow.center = clockwise ? std::array<float, 2>{c + 0.44F * size, c - 0.28F * size}
                                     : std::array<float, 2>{c + 0.28F * size, c - 0.44F * size};
            arrow.direction = face;
            const Vec3 toBottom = clockwise ? right : right * -1.0F;
            arrow.lean = face.z > 0.0F ? toBottom : toBottom * -1.0F;
            arrows.push_back(arrow);
        }
    }
    return arrows;
}

int viewCubeArrowHit(const Camera& camera, float size, float x, float y) {
    const std::vector<ViewCubeArrow> arrows = viewCubeArrows(camera, size);
    const float reach = kArrowReach * size;
    for (std::size_t i = 0; i < arrows.size(); ++i) {
        const auto& [cx, cy] = arrows[i].center;
        if ((x - cx) * (x - cx) + (y - cy) * (y - cy) <= reach * reach) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

ViewCubeCompass viewCubeCompass(const Camera& camera, float size) {
    ViewCubeCompass compass;
    if (std::abs(camera.forward().z) < kCompassMinTilt) {
        return compass;
    }
    const Vec3 right = camera.right();
    const Vec3 up = camera.up();
    const float center = size * 0.5F;
    const float scale = size * kScale;
    const auto onScreen = [&](float angle) {
        const Vec3 p{std::cos(angle) * kCompassRadius, std::sin(angle) * kCompassRadius, -1.0F};
        return std::array<float, 2>{center + dot(p, right) * scale, center - dot(p, up) * scale};
    };
    constexpr int kSegments = 48;
    for (int k = 0; k <= kSegments; ++k) {
        const auto point = onScreen(2.0F * std::numbers::pi_v<float> * static_cast<float>(k) /
                                    static_cast<float>(kSegments));
        compass.ring.insert(compass.ring.end(), point.begin(), point.end());
    }
    // N along +Y, E along +X, S along -Y, W along -X.
    const float quarter = std::numbers::pi_v<float> * 0.5F;
    compass.labels = {onScreen(quarter), onScreen(0.0F), onScreen(-quarter),
                      onScreen(2.0F * quarter)};
    return compass;
}

bool viewCubeCompassHit(const Camera& camera, float size, float x, float y) {
    const ViewCubeCompass compass = viewCubeCompass(camera, size);
    const float reach = kCompassReach * size;
    for (const auto& [lx, ly] : compass.labels) {
        if (!compass.ring.empty() && std::hypot(x - lx, y - ly) <= reach) {
            return true;
        }
    }
    for (std::size_t i = 2; i + 1 < compass.ring.size(); i += 2) {
        // Distance to the segment from the previous point.
        const float ax = compass.ring[i - 2];
        const float ay = compass.ring[i - 1];
        const float dx = compass.ring[i] - ax;
        const float dy = compass.ring[i + 1] - ay;
        const float lengthSquared = dx * dx + dy * dy;
        const float t =
            lengthSquared > 0.0F
                ? std::clamp(((x - ax) * dx + (y - ay) * dy) / lengthSquared, 0.0F, 1.0F)
                : 0.0F;
        if (std::hypot(x - (ax + dx * t), y - (ay + dy * t)) <= reach) {
            return true;
        }
    }
    return false;
}

}  // namespace facetry::render
