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

// Regions are the closed areas of a sketch, its profiles (Renderer::setRegions).
enum class PickKind : std::uint32_t {
    None = 0,
    Face = 1,
    Edge = 2,
    Vertex = 3,
    Body = 4,
    Region = 5,
    SketchCurve = 6,  // a curve of the sketch being edited (Renderer::setSketch)
    SketchPoint = 7,  // a point of it
    Plane = 8,        // an origin plane shown to start a sketch on (Renderer::setOriginPlanes)
};

// A face, edge, vertex, body or sketch region, by ID.
struct Element {
    PickKind kind = PickKind::None;
    std::uint32_t id = 0;

    friend bool operator==(const Element&, const Element&) = default;
};

// Box selection (#37): from left to right a window takes what lies entirely inside the box, from
// right to left a crossing box also what it touches, like in Fusion.
enum class BoxMode : std::uint8_t { Window, Crossing };

// What picking looks for (selection modes, keys 1–5 like Plasticity's).
enum class PickFilter : std::uint32_t {
    Everything = 0,  // vertices before edges before faces, by the pick radius
    Vertices = 1,
    Edges = 2,
    Faces = 3,
    Bodies = 4,  // the body under the pointer; the shown model is body 1 until there are several
    Regions =
        5,  // only the sketch regions shown (Renderer::setRegions); "Everything" takes them too
};

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
    // Every face the ray passes, nearest first, each once.
    [[nodiscard]] std::vector<RayHit> raycastAll(const Ray& ray) const;
    // Bounds of one face, edge or vertex; empty if there is no such element.
    [[nodiscard]] Bounds elementBounds(PickKind kind, std::uint32_t id) const;

    // Picks the element under (x, y). Vertices win over edges, edges over faces, as long as they
    // are within `radiusPoints` of the pointer and not hidden behind a face. With `throughFaces`
    // (wireframe, transparent bodies) hidden vertices and edges count too.
    [[nodiscard]] PickResult pick(const Camera& camera, float x, float y, float radiusPoints,
                                  bool throughFaces = false,
                                  PickFilter filter = PickFilter::Everything) const;

    // Everything a click at (x, y) could mean, best first (#38): vertices, then edges, by their
    // distance within `radiusPoints`, then the faces along the ray from front to back, also the
    // hidden ones. The first candidate is what pick() returns.
    [[nodiscard]] std::vector<PickResult> candidates(
        const Camera& camera, float x, float y, float radiusPoints, bool throughFaces = false,
        PickFilter filter = PickFilter::Everything) const;

    // The elements in the screen box between (x0, y0) and (x1, y1), in ID order. "Everything"
    // takes edges, the usual reason for a box (fillets); the other filters take their kind.
    // Elements hidden behind faces stay out unless `throughFaces`.
    [[nodiscard]] std::vector<Element> boxSelect(const Camera& camera, float x0, float y0, float x1,
                                                 float y1, BoxMode mode, PickFilter filter,
                                                 bool throughFaces) const;

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

// The origin planes to start a sketch on (#61): squares from the origin into the positive quadrant
// of XY (face ID 1), XZ (2) and YZ (3), at least 10 units and 1.2 times the scene's largest
// coordinate, with outlines as edges 1 to 3.
[[nodiscard]] RenderMesh originPlanesMesh(const Bounds& scene);

// The plane of `planes` the ray meets first, unless it meets a face of `model` too: planes are
// translucent and stand out from the model, so a face under the pointer is what the user means.
[[nodiscard]] std::optional<PickResult> pickPlane(const PickIndex& planes, const PickIndex& model,
                                                  const Ray& ray);

// What is drawn over the model and picked before it: a sketch's regions to choose profiles from
// (Renderer::setRegions) and the sketch being edited (Renderer::setSketch). Either may be empty.
struct Overlays {
    const PickIndex& regions;
    const PickIndex& sketch;
};

// Picking with the overlays first, whatever lies in front of them, since they are drawn over the
// model. With "Everything" the region under (x, y) comes first, then the sketch's points and
// curves within the radius; "Regions" takes only regions.
[[nodiscard]] PickResult pickWithOverlays(const PickIndex& model, Overlays overlays,
                                          const Camera& camera, float x, float y,
                                          float radiusPoints, bool throughFaces, PickFilter filter);
[[nodiscard]] std::vector<PickResult> candidatesWithOverlays(const PickIndex& model,
                                                             Overlays overlays,
                                                             const Camera& camera, float x, float y,
                                                             float radiusPoints, bool throughFaces,
                                                             PickFilter filter);

}  // namespace facetry::render
