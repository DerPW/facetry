#pragma once

#include "facetry/render/camera.hpp"
#include "facetry/render/math.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace facetry::render {

// Mesh as produced by the core (see facetry_mesh_buffer_kind in facetry_core.h).
struct RenderMesh {
    std::vector<float> positions;  // xyz per mesh vertex
    std::vector<float> normals;    // xyz per mesh vertex
    std::vector<std::uint32_t> triangles;
    std::vector<std::uint32_t> triangleFaceIds;
    std::vector<float> edgePoints;  // xyz
    std::vector<std::uint32_t> edgeOffsets{0};
    std::vector<std::uint32_t> edgeIds;
    std::vector<float> vertexPositions;  // xyz
    std::vector<std::uint32_t> vertexIds;

    [[nodiscard]] std::size_t triangleCount() const {
        return triangles.size() / 3;
    }
};

enum class PickKind : std::uint32_t { None = 0, Face = 1, Edge = 2, Vertex = 3 };

struct PickResult {
    PickKind kind = PickKind::None;
    std::uint32_t id = 0;
    Vec3 point;                   // picked point in world space
    float screenDistance = 0.0F;  // points between the pointer and the picked element
};

struct RayHit {
    float distance = 0.0F;
    std::uint32_t triangle = 0;
    std::uint32_t faceId = 0;
};

// CPU picking: a BVH over the triangles for faces, screen-space distance for edges and vertices
// (docs/adr/0004-viewport-renderer-shared-cpp-sokol.md).
class PickIndex {
  public:
    PickIndex() = default;
    explicit PickIndex(const RenderMesh& mesh);

    [[nodiscard]] const Bounds& bounds() const {
        return bounds_;
    }
    [[nodiscard]] std::optional<RayHit> raycast(const Ray& ray) const;
    // Bounds of one face, edge or vertex; empty if there is no such element.
    [[nodiscard]] Bounds elementBounds(PickKind kind, std::uint32_t id) const;

    // Picks the element under (x, y). Vertices win over edges, edges over faces, as long as they
    // are within `radiusPoints` of the pointer and not hidden behind a face.
    [[nodiscard]] PickResult pick(const Camera& camera, float x, float y, float radiusPoints) const;

    // Point under the pointer for orbiting and zooming: the hit point on the model, or the point
    // on the camera's target plane if nothing is hit.
    [[nodiscard]] Vec3 anchorAt(const Camera& camera, float x, float y) const;

  private:
    struct Node {
        Bounds box;
        std::uint32_t first = 0;  // leaf: first triangle in order_; inner: left child index
        std::uint32_t count = 0;  // leaf: triangle count; inner: 0
        std::uint32_t right = 0;  // inner: right child index
    };

    std::uint32_t build(std::uint32_t first, std::uint32_t count, std::vector<Vec3>& centroids);
    [[nodiscard]] Vec3 vertex(std::uint32_t index) const;
    [[nodiscard]] bool isVisible(const Camera& camera, Vec3 point) const;

    std::vector<Vec3> positions_;
    std::vector<std::uint32_t> triangles_;
    std::vector<std::uint32_t> faceIds_;
    std::vector<std::uint32_t> order_;  // triangle indices, grouped by leaf
    std::vector<Node> nodes_;

    struct Segment {
        Vec3 a;
        Vec3 b;
        std::uint32_t edgeId = 0;
    };
    std::vector<Segment> segments_;
    std::vector<Vec3> vertexPoints_;
    std::vector<std::uint32_t> vertexIds_;
    Bounds bounds_;
};

}  // namespace facetry::render
