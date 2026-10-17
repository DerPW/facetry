#include "facetry/render/pick.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>

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
            // A mesh of only curves (a sketch) has its bounds from them.
            bounds_.add(segments_.back().a);
            bounds_.add(segments_.back().b);
        }
    }
    for (std::size_t v = 0; v < mesh.vertexIds.size(); ++v) {
        vertexPoints_.push_back(point3(mesh.vertexPositions, v));
        vertexIds_.push_back(mesh.vertexIds[v]);
        bounds_.add(vertexPoints_.back());
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

std::vector<RayHit> PickIndex::raycastAll(const Ray& ray) const {
    std::vector<RayHit> hits;
    if (nodes_.empty()) {
        return hits;
    }
    std::uint32_t stack[64];
    int top = 0;
    stack[top++] = 0;
    while (top > 0) {
        const Node& node = nodes_[stack[--top]];
        if (intersect(ray, node.box, std::numeric_limits<float>::max()) < 0.0F) {
            continue;
        }
        if (node.count > 0) {
            for (std::uint32_t i = node.first; i < node.first + node.count; ++i) {
                const std::uint32_t t = order_[i];
                const float d =
                    intersectTriangle(ray, vertex(t * 3), vertex(t * 3 + 1), vertex(t * 3 + 2));
                if (d > 0.0F) {
                    hits.push_back({d, t, t < faceIds_.size() ? faceIds_[t] : 0});
                }
            }
        } else if (top + 2 <= 64) {
            stack[top++] = node.first;
            stack[top++] = node.right;
        }
    }
    std::sort(hits.begin(), hits.end(),
              [](const RayHit& a, const RayHit& b) { return a.distance < b.distance; });
    // Each face once, where the ray first meets it.
    std::vector<RayHit> faces;
    for (const RayHit& hit : hits) {
        if (std::none_of(faces.begin(), faces.end(),
                         [&](const RayHit& face) { return face.faceId == hit.faceId; })) {
            faces.push_back(hit);
        }
    }
    return faces;
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

PickResult PickIndex::pick(const Camera& camera, float x, float y, float radiusPoints,
                           bool throughFaces, PickFilter filter) const {
    const float radius2 = radiusPoints * radiusPoints;
    const bool vertices = filter == PickFilter::Everything || filter == PickFilter::Vertices;
    const bool edges = filter == PickFilter::Everything || filter == PickFilter::Edges;
    const bool faces = filter != PickFilter::Vertices && filter != PickFilter::Edges;

    // Vertices first: they are the smallest targets.
    PickResult vertexPick;
    float best = radius2;
    for (std::size_t i = 0; vertices && i < vertexPoints_.size(); ++i) {
        const auto p = camera.project(vertexPoints_[i]);
        if (p.w <= 0.0F) {
            continue;
        }
        const float d2 = (p.x - x) * (p.x - x) + (p.y - y) * (p.y - y);
        if (d2 <= best && (throughFaces || isVisible(camera, vertexPoints_[i]))) {
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
        if (!edges) {
            break;
        }
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
        if (throughFaces || isVisible(camera, point)) {
            best = d2;
            edgePick = {PickKind::Edge, segment.edgeId, point, std::sqrt(d2)};
        }
    }
    if (edgePick.kind != PickKind::None) {
        return edgePick;
    }

    if (!faces) {
        return {};
    }
    const Ray ray = camera.rayAt(x, y);
    if (const auto hit = raycast(ray)) {
        const Vec3 point = ray.origin + ray.direction * hit->distance;
        if (filter == PickFilter::Bodies) {
            return {PickKind::Body, 1, point, 0.0F};
        }
        return {PickKind::Face, hit->faceId, point, 0.0F};
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
        case PickKind::Body:
            if (id == 1) {
                bounds = bounds_;
            }
            break;
        case PickKind::Region:  // regions and sketches have indexes of their own
        case PickKind::SketchCurve:
        case PickKind::SketchPoint:
        case PickKind::None:
            break;
    }
    return bounds;
}

std::vector<PickResult> PickIndex::candidates(const Camera& camera, float x, float y,
                                              float radiusPoints, bool throughFaces,
                                              PickFilter filter) const {
    const float radius2 = radiusPoints * radiusPoints;
    std::vector<PickResult> result;
    const auto byDistance = [](const PickResult& a, const PickResult& b) {
        return a.screenDistance < b.screenDistance;
    };

    if (filter == PickFilter::Everything || filter == PickFilter::Vertices) {
        std::vector<PickResult> vertices;
        for (std::size_t i = 0; i < vertexPoints_.size(); ++i) {
            const auto p = camera.project(vertexPoints_[i]);
            const float d2 = (p.x - x) * (p.x - x) + (p.y - y) * (p.y - y);
            if (p.w > 0.0F && d2 <= radius2 &&
                (throughFaces || isVisible(camera, vertexPoints_[i]))) {
                vertices.push_back(
                    {PickKind::Vertex, vertexIds_[i], vertexPoints_[i], std::sqrt(d2)});
            }
        }
        std::stable_sort(vertices.begin(), vertices.end(), byDistance);
        result.insert(result.end(), vertices.begin(), vertices.end());
    }

    if (filter == PickFilter::Everything || filter == PickFilter::Edges) {
        // The nearest visible point of each edge within the radius.
        std::map<std::uint32_t, PickResult> edges;
        for (const Segment& segment : segments_) {
            const auto a = camera.project(segment.a);
            const auto b = camera.project(segment.b);
            if (a.w <= 0.0F || b.w <= 0.0F) {
                continue;
            }
            const float abx = b.x - a.x;
            const float aby = b.y - a.y;
            const float lengthSquared = abx * abx + aby * aby;
            float t = 0.0F;
            if (lengthSquared > 1e-12F) {
                t = std::clamp(((x - a.x) * abx + (y - a.y) * aby) / lengthSquared, 0.0F, 1.0F);
            }
            const float px = a.x + abx * t - x;
            const float py = a.y + aby * t - y;
            const float d2 = px * px + py * py;
            if (d2 > radius2) {
                continue;
            }
            const auto known = edges.find(segment.edgeId);
            if (known != edges.end() && known->second.screenDistance <= std::sqrt(d2)) {
                continue;
            }
            const Vec3 point = lerp(segment.a, segment.b, t);
            if (throughFaces || isVisible(camera, point)) {
                edges[segment.edgeId] = {PickKind::Edge, segment.edgeId, point, std::sqrt(d2)};
            }
        }
        std::vector<PickResult> sorted;
        for (const auto& [id, edge] : edges) {
            sorted.push_back(edge);
        }
        std::stable_sort(sorted.begin(), sorted.end(), byDistance);
        result.insert(result.end(), sorted.begin(), sorted.end());
    }

    if (filter != PickFilter::Vertices && filter != PickFilter::Edges) {
        const Ray ray = camera.rayAt(x, y);
        const auto hits = raycastAll(ray);
        if (filter == PickFilter::Bodies) {
            if (!hits.empty()) {
                result.push_back(
                    {PickKind::Body, 1, ray.origin + ray.direction * hits[0].distance, 0.0F});
            }
        } else {
            for (const RayHit& hit : hits) {
                result.push_back(
                    {PickKind::Face, hit.faceId, ray.origin + ray.direction * hit.distance, 0.0F});
            }
        }
    }
    return result;
}

namespace {

struct ScreenBox {
    float minX;
    float minY;
    float maxX;
    float maxY;

    [[nodiscard]] bool contains(float x, float y) const {
        return x >= minX && x <= maxX && y >= minY && y <= maxY;
    }
};

// Whether the screen segment a–b touches the box (Liang–Barsky clipping).
bool segmentTouches(const ScreenBox& box, float ax, float ay, float bx, float by) {
    float t0 = 0.0F;
    float t1 = 1.0F;
    const float dx = bx - ax;
    const float dy = by - ay;
    const std::array<float, 4> p{-dx, dx, -dy, dy};
    const std::array<float, 4> q{ax - box.minX, box.maxX - ax, ay - box.minY, box.maxY - ay};
    for (std::size_t i = 0; i < 4; ++i) {
        if (std::abs(p[i]) < 1e-12F) {
            if (q[i] < 0.0F) {
                return false;
            }
            continue;
        }
        const float t = q[i] / p[i];
        if (p[i] < 0.0F) {
            t0 = std::max(t0, t);
        } else {
            t1 = std::min(t1, t);
        }
        if (t0 > t1) {
            return false;
        }
    }
    return true;
}

// Whether the screen point (x, y) lies in the triangle a, b, c.
bool inTriangle(float x, float y, const Camera::Projected& a, const Camera::Projected& b,
                const Camera::Projected& c) {
    const auto side = [](float px, float py, const Camera::Projected& u,
                         const Camera::Projected& v) {
        return (v.x - u.x) * (py - u.y) - (v.y - u.y) * (px - u.x);
    };
    const float d0 = side(x, y, a, b);
    const float d1 = side(x, y, b, c);
    const float d2 = side(x, y, c, a);
    return (d0 >= 0 && d1 >= 0 && d2 >= 0) || (d0 <= 0 && d1 <= 0 && d2 <= 0);
}

}  // namespace

std::vector<Element> PickIndex::boxSelect(const Camera& camera, float x0, float y0, float x1,
                                          float y1, BoxMode mode, PickFilter filter,
                                          bool throughFaces) const {
    const ScreenBox box{std::min(x0, x1), std::min(y0, y1), std::max(x0, x1), std::max(y0, y1)};
    const bool window = mode == BoxMode::Window;
    std::vector<Element> result;

    if (filter == PickFilter::Vertices) {
        for (std::size_t i = 0; i < vertexPoints_.size(); ++i) {
            const auto p = camera.project(vertexPoints_[i]);
            if (p.w > 0.0F && box.contains(p.x, p.y) &&
                (throughFaces || isVisible(camera, vertexPoints_[i]))) {
                result.push_back({PickKind::Vertex, vertexIds_[i]});
            }
        }
    } else if (filter == PickFilter::Edges || filter == PickFilter::Everything) {
        // Per edge: inside (window: every segment inside; crossing: any segment touching) and
        // visible somewhere.
        std::map<std::uint32_t, std::pair<bool, bool>> edges;  // id -> (in box, visible)
        for (const Segment& segment : segments_) {
            const auto a = camera.project(segment.a);
            const auto b = camera.project(segment.b);
            auto& [inBox, visible] = edges.try_emplace(segment.edgeId, window, false).first->second;
            const bool front = a.w > 0.0F && b.w > 0.0F;
            if (window) {
                inBox = inBox && front && box.contains(a.x, a.y) && box.contains(b.x, b.y);
            } else {
                inBox = inBox || (front && segmentTouches(box, a.x, a.y, b.x, b.y));
            }
            if (!visible && front) {
                visible = throughFaces || isVisible(camera, lerp(segment.a, segment.b, 0.5F));
            }
        }
        for (const auto& [id, state] : edges) {
            if (state.first && state.second) {
                result.push_back({PickKind::Edge, id});
            }
        }
    } else {
        // Faces, or the body: by their triangles; a face is visible where a ray through one of
        // its triangles in the box hits it first.
        std::map<std::uint32_t, std::pair<bool, bool>> faces;  // id -> (in box, visible)
        for (std::size_t t = 0; t < faceIds_.size(); ++t) {
            const auto first = static_cast<std::uint32_t>(t * 3);
            const auto a = camera.project(vertex(first));
            const auto b = camera.project(vertex(first + 1));
            const auto c = camera.project(vertex(first + 2));
            auto& [inBox, visible] = faces.try_emplace(faceIds_[t], window, false).first->second;
            const bool front = a.w > 0.0F && b.w > 0.0F && c.w > 0.0F;
            const bool inside =
                front && box.contains(a.x, a.y) && box.contains(b.x, b.y) && box.contains(c.x, c.y);
            const bool touches = front && (inside || segmentTouches(box, a.x, a.y, b.x, b.y) ||
                                           segmentTouches(box, b.x, b.y, c.x, c.y) ||
                                           segmentTouches(box, c.x, c.y, a.x, a.y) ||
                                           inTriangle(box.minX, box.minY, a, b, c));
            inBox = window ? inBox && inside : inBox || touches;
            if (!visible && touches) {
                if (throughFaces) {
                    visible = true;
                } else {
                    const Ray ray =
                        camera.rayAt((a.x + b.x + c.x) / 3.0F, (a.y + b.y + c.y) / 3.0F);
                    const auto hit = raycast(ray);
                    visible = hit && hit->faceId == faceIds_[t];
                }
            }
        }
        if (filter == PickFilter::Bodies) {
            // The shown model is body 1: in the box as a whole (window) or touched (crossing).
            bool all = !faces.empty();
            bool any = false;
            for (const auto& [id, state] : faces) {
                all = all && state.first;
                any = any || (state.first && state.second);
            }
            if (window ? all : any) {
                result.push_back({PickKind::Body, 1});
            }
        } else {
            for (const auto& [id, state] : faces) {
                if (state.first && state.second) {
                    result.push_back({PickKind::Face, id});
                }
            }
        }
    }
    return result;
}

namespace {

std::optional<PickResult> pickRegion(const PickIndex& regions, const Camera& camera, float x,
                                     float y, PickFilter filter) {
    if ((filter != PickFilter::Everything && filter != PickFilter::Regions) ||
        regions.bounds().isEmpty()) {
        return std::nullopt;
    }
    const Ray ray = camera.rayAt(x, y);
    const auto hit = regions.raycast(ray);
    if (!hit) {
        return std::nullopt;
    }
    return PickResult{.kind = PickKind::Region,
                      .id = hit->faceId,
                      .point = ray.origin + ray.direction * hit->distance,
                      .screenDistance = 0.0F};
}

}  // namespace

// The sketch's points and curves within the radius of (x, y), points first.
std::vector<PickResult> sketchCandidates(const PickIndex& sketch, const Camera& camera, float x,
                                         float y, float radiusPoints, PickFilter filter) {
    if (filter != PickFilter::Everything || sketch.bounds().isEmpty()) {
        return {};
    }
    std::vector<PickResult> result =
        sketch.candidates(camera, x, y, radiusPoints, /*throughFaces=*/true, filter);
    std::erase_if(result, [](const PickResult& pick) {
        return pick.kind != PickKind::Vertex && pick.kind != PickKind::Edge;
    });
    for (PickResult& pick : result) {
        pick.kind = pick.kind == PickKind::Vertex ? PickKind::SketchPoint : PickKind::SketchCurve;
    }
    return result;
}

PickResult pickWithOverlays(const PickIndex& model, Overlays overlays, const Camera& camera,
                            float x, float y, float radiusPoints, bool throughFaces,
                            PickFilter filter) {
    if (const auto region = pickRegion(overlays.regions, camera, x, y, filter)) {
        return *region;
    }
    if (filter == PickFilter::Regions) {
        return {};
    }
    const auto sketch = sketchCandidates(overlays.sketch, camera, x, y, radiusPoints, filter);
    if (!sketch.empty()) {
        return sketch.front();
    }
    return model.pick(camera, x, y, radiusPoints, throughFaces, filter);
}

std::vector<PickResult> candidatesWithOverlays(const PickIndex& model, Overlays overlays,
                                               const Camera& camera, float x, float y,
                                               float radiusPoints, bool throughFaces,
                                               PickFilter filter) {
    std::vector<PickResult> result;
    if (const auto region = pickRegion(overlays.regions, camera, x, y, filter)) {
        result.push_back(*region);
    }
    if (filter != PickFilter::Regions) {
        const auto sketch = sketchCandidates(overlays.sketch, camera, x, y, radiusPoints, filter);
        result.insert(result.end(), sketch.begin(), sketch.end());
        const auto rest = model.candidates(camera, x, y, radiusPoints, throughFaces, filter);
        result.insert(result.end(), rest.begin(), rest.end());
    }
    return result;
}

}  // namespace facetry::render
