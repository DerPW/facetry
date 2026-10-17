#include "facetry/render/pick.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace facetry::render {
namespace {

constexpr std::uint32_t kLeafSize = 4;

Vec3 point3(const std::vector<float>& xyz, std::size_t index) {
    return {xyz[index * 3], xyz[index * 3 + 1], xyz[index * 3 + 2]};
}

// Möller-Trumbore; returns the distance along the ray or a negative value.
float intersectTriangle(const Ray& ray, Vec3 a, Vec3 b, Vec3 c) {
    const Vec3 e1 = b - a;
    const Vec3 e2 = c - a;
    const Vec3 p = cross(ray.direction, e2);
    const float det = dot(e1, p);
    if (std::abs(det) < 1e-12F) {
        return -1.0F;
    }
    const float inverse = 1.0F / det;
    const Vec3 s = ray.origin - a;
    const float u = dot(s, p) * inverse;
    if (u < 0.0F || u > 1.0F) {
        return -1.0F;
    }
    const Vec3 q = cross(s, e1);
    const float v = dot(ray.direction, q) * inverse;
    if (v < 0.0F || u + v > 1.0F) {
        return -1.0F;
    }
    return dot(e2, q) * inverse;
}

}  // namespace

PickIndex::PickIndex(const RenderMesh& mesh) {
    const std::size_t vertexCount = mesh.positions.size() / 3;
    positions_.reserve(vertexCount);
    for (std::size_t i = 0; i < vertexCount; ++i) {
        positions_.push_back(point3(mesh.positions, i));
        bounds_.add(positions_.back());
    }
    triangles_ = mesh.triangles;
    faceIds_ = mesh.triangleFaceIds;

    const auto triangleCount = static_cast<std::uint32_t>(mesh.triangleCount());
    order_.resize(triangleCount);
    std::vector<Vec3> centroids(triangleCount);
    for (std::uint32_t t = 0; t < triangleCount; ++t) {
        order_[t] = t;
        centroids[t] = (vertex(t * 3) + vertex(t * 3 + 1) + vertex(t * 3 + 2)) * (1.0F / 3.0F);
    }
    if (triangleCount > 0) {
        nodes_.reserve(std::size_t{triangleCount} * 2 / kLeafSize + 1);
        build(0, triangleCount, centroids);
    }

    for (std::size_t e = 0; e + 1 < mesh.edgeOffsets.size() && e < mesh.edgeIds.size(); ++e) {
        for (std::uint32_t k = mesh.edgeOffsets[e]; k + 1 < mesh.edgeOffsets[e + 1]; ++k) {
            segments_.push_back(
                {point3(mesh.edgePoints, k), point3(mesh.edgePoints, k + 1), mesh.edgeIds[e]});
        }
    }
    for (std::size_t v = 0; v < mesh.vertexIds.size(); ++v) {
        vertexPoints_.push_back(point3(mesh.vertexPositions, v));
        vertexIds_.push_back(mesh.vertexIds[v]);
    }
}

Vec3 PickIndex::vertex(std::uint32_t index) const {
    return positions_[triangles_[index]];
}

std::uint32_t PickIndex::build(std::uint32_t first, std::uint32_t count,
                               std::vector<Vec3>& centroids) {
    const auto index = static_cast<std::uint32_t>(nodes_.size());
    nodes_.push_back({});
    Bounds box;
    Bounds centroidBox;
    for (std::uint32_t i = first; i < first + count; ++i) {
        const std::uint32_t t = order_[i];
        box.add(vertex(t * 3));
        box.add(vertex(t * 3 + 1));
        box.add(vertex(t * 3 + 2));
        centroidBox.add(centroids[t]);
    }
    nodes_[index].box = box;

    if (count <= kLeafSize) {
        nodes_[index].first = first;
        nodes_[index].count = count;
        return index;
    }
    // Median split along the longest axis of the centroids.
    const Vec3 extent = centroidBox.max - centroidBox.min;
    const int axis =
        extent.x >= extent.y && extent.x >= extent.z ? 0 : (extent.y >= extent.z ? 1 : 2);
    const auto key = [&](std::uint32_t t) {
        return axis == 0 ? centroids[t].x : (axis == 1 ? centroids[t].y : centroids[t].z);
    };
    const std::uint32_t half = count / 2;
    const auto begin = order_.begin() + static_cast<std::ptrdiff_t>(first);
    std::nth_element(begin, begin + static_cast<std::ptrdiff_t>(half),
                     begin + static_cast<std::ptrdiff_t>(count),
                     [&](std::uint32_t a, std::uint32_t b) { return key(a) < key(b); });
    const std::uint32_t left = build(first, half, centroids);
    const std::uint32_t right = build(first + half, count - half, centroids);
    nodes_[index].first = left;
    nodes_[index].right = right;
    nodes_[index].count = 0;
    return index;
}

std::optional<RayHit> PickIndex::raycast(const Ray& ray) const {
    if (nodes_.empty()) {
        return std::nullopt;
    }
    std::optional<RayHit> best;
    float bestDistance = std::numeric_limits<float>::max();
    std::uint32_t stack[64];
    int top = 0;
    stack[top++] = 0;
    while (top > 0) {
        const Node& node = nodes_[stack[--top]];
        if (intersect(ray, node.box, bestDistance) < 0.0F) {
            continue;
        }
        if (node.count > 0) {
            for (std::uint32_t i = node.first; i < node.first + node.count; ++i) {
                const std::uint32_t t = order_[i];
                const float d =
                    intersectTriangle(ray, vertex(t * 3), vertex(t * 3 + 1), vertex(t * 3 + 2));
                if (d > 0.0F && d < bestDistance) {
                    bestDistance = d;
                    best = RayHit{d, t, t < faceIds_.size() ? faceIds_[t] : 0};
                }
            }
        } else if (top + 2 <= 64) {
            stack[top++] = node.first;
            stack[top++] = node.right;
        }
    }
    return best;
}

bool PickIndex::isVisible(const Camera& camera, Vec3 point) const {
    // Cast a ray from the camera to the point: it is visible if no face is clearly in front.
    Ray ray;
    float distanceToPoint = 0.0F;
    if (camera.projection() == Camera::Projection::Orthographic) {
        const float back = bounds_.radius() * 4.0F + 1.0F;
        ray = {point - camera.forward() * back, camera.forward()};
        distanceToPoint = back;
    } else {
        const Vec3 toPoint = point - camera.eye();
        distanceToPoint = length(toPoint);
        ray = {camera.eye(), toPoint * (1.0F / std::max(distanceToPoint, 1e-12F))};
    }
    const auto hit = raycast(ray);
    const float tolerance = std::max(bounds_.radius(), 1e-3F) * 1e-3F;
    return !hit || hit->distance >= distanceToPoint - tolerance;
}

PickResult PickIndex::pick(const Camera& camera, float x, float y, float radiusPoints) const {
    const float radius2 = radiusPoints * radiusPoints;

    // Vertices first: they are the smallest targets.
    PickResult vertexPick;
    float best = radius2;
    for (std::size_t i = 0; i < vertexPoints_.size(); ++i) {
        const auto p = camera.project(vertexPoints_[i]);
        if (p.w <= 0.0F) {
            continue;
        }
        const float d2 = (p.x - x) * (p.x - x) + (p.y - y) * (p.y - y);
        if (d2 <= best && isVisible(camera, vertexPoints_[i])) {
            best = d2;
            vertexPick = {PickKind::Vertex, vertexIds_[i], vertexPoints_[i], std::sqrt(d2)};
        }
    }
    if (vertexPick.kind != PickKind::None) {
        return vertexPick;
    }

    // Then edges, by distance to their projected segments.
    PickResult edgePick;
    best = radius2;
    for (const Segment& segment : segments_) {
        const auto a = camera.project(segment.a);
        const auto b = camera.project(segment.b);
        if (a.w <= 0.0F || b.w <= 0.0F) {
            continue;
        }
        const float abx = b.x - a.x;
        const float aby = b.y - a.y;
        const float lengthSquared = abx * abx + aby * aby;
        float s = 0.0F;
        if (lengthSquared > 1e-12F) {
            s = std::clamp(((x - a.x) * abx + (y - a.y) * aby) / lengthSquared, 0.0F, 1.0F);
        }
        const float px = a.x + abx * s - x;
        const float py = a.y + aby * s - y;
        const float d2 = px * px + py * py;
        if (d2 > best) {
            continue;
        }
        // Screen-space interpolation is close enough for picking; perspective correction is
        // not needed at the scale of a pick radius.
        const Vec3 point = lerp(segment.a, segment.b, s);
        if (isVisible(camera, point)) {
            best = d2;
            edgePick = {PickKind::Edge, segment.edgeId, point, std::sqrt(d2)};
        }
    }
    if (edgePick.kind != PickKind::None) {
        return edgePick;
    }

    const Ray ray = camera.rayAt(x, y);
    if (const auto hit = raycast(ray)) {
        return {PickKind::Face, hit->faceId, ray.origin + ray.direction * hit->distance, 0.0F};
    }
    return {};
}

Vec3 PickIndex::anchorAt(const Camera& camera, float x, float y) const {
    const Ray ray = camera.rayAt(x, y);
    if (const auto hit = raycast(ray)) {
        return ray.origin + ray.direction * hit->distance;
    }
    return camera.pointOnTargetPlane(x, y);
}

Bounds PickIndex::elementBounds(PickKind kind, std::uint32_t id) const {
    Bounds bounds;
    switch (kind) {
        case PickKind::Face:
            for (std::size_t t = 0; t < faceIds_.size(); ++t) {
                if (faceIds_[t] == id) {
                    for (std::uint32_t corner = 0; corner < 3; ++corner) {
                        bounds.add(vertex(static_cast<std::uint32_t>(t * 3) + corner));
                    }
                }
            }
            break;
        case PickKind::Edge:
            for (const Segment& segment : segments_) {
                if (segment.edgeId == id) {
                    bounds.add(segment.a);
                    bounds.add(segment.b);
                }
            }
            break;
        case PickKind::Vertex:
            for (std::size_t v = 0; v < vertexIds_.size(); ++v) {
                if (vertexIds_[v] == id) {
                    bounds.add(vertexPoints_[v]);
                }
            }
            break;
        case PickKind::None:
            break;
    }
    return bounds;
}

}  // namespace facetry::render
