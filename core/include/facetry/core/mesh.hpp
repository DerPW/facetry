#pragma once

#include "facetry/core/shape.hpp"

#include <cstdint>
#include <vector>

namespace facetry::core {

struct TessellationOptions {
    // Maximum distance between mesh and surface, in model units (or as a fraction of the shape
    // size if `relative` is set).
    double linearDeflection = 0.05;
    // Maximum angle between neighboring facets, in radians.
    double angularDeflection = 0.35;
    bool relative = false;
};

// Display mesh of a shape, laid out for direct GPU upload. See facetry_mesh_buffer_kind in
// facetry_core.h for the exact meaning of each buffer.
struct Mesh {
    std::vector<float> positions;                // xyz per mesh vertex (not shared between faces)
    std::vector<float> normals;                  // xyz per mesh vertex
    std::vector<std::uint32_t> triangles;        // 3 vertex indices per triangle, CCW
    std::vector<std::uint32_t> triangleFaceIds;  // face ID per triangle

    std::vector<float> edgePoints;              // xyz, all edge polylines in a row
    std::vector<std::uint32_t> edgeOffsets{0};  // edge i: points [offsets[i], offsets[i+1])
    std::vector<std::uint32_t> edgeIds;         // edge ID per polyline

    std::vector<float> vertexPositions;  // xyz per topological vertex
    std::vector<std::uint32_t> vertexIds;

    [[nodiscard]] std::size_t triangleCount() const noexcept {
        return triangles.size() / 3;
    }
    [[nodiscard]] std::size_t meshVertexCount() const noexcept {
        return positions.size() / 3;
    }
    [[nodiscard]] std::size_t edgeCount() const noexcept {
        return edgeIds.size();
    }
};

[[nodiscard]] Mesh tessellate(const Shape& shape, const TessellationOptions& options = {});

}  // namespace facetry::core
