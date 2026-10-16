// A unit cube as the core would tessellate it: 6 faces with their own vertices, 12 edges and
// 8 vertices, all with IDs. Face IDs: 1 -X, 2 +X, 3 -Y, 4 +Y, 5 -Z, 6 +Z.
#pragma once

#include "facetry/render/pick.hpp"

#include <array>

namespace facetry::render::test {

inline RenderMesh unitCube(float size = 1.0F) {
    RenderMesh mesh;
    const auto corner = [size](int i) {
        return Vec3{(i & 1) != 0 ? size : 0.0F, (i & 2) != 0 ? size : 0.0F,
                    (i & 4) != 0 ? size : 0.0F};
    };
    // Corners of each face, counter-clockwise seen from outside, and its normal.
    const std::array<std::array<int, 4>, 6> faces = {{
        {0, 4, 6, 2},  // -X
        {1, 3, 7, 5},  // +X
        {0, 1, 5, 4},  // -Y
        {2, 6, 7, 3},  // +Y
        {0, 2, 3, 1},  // -Z
        {4, 5, 7, 6},  // +Z
    }};
    const std::array<Vec3, 6> normals = {
        {{-1, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}}};
    for (std::uint32_t f = 0; f < 6; ++f) {
        const auto base = static_cast<std::uint32_t>(mesh.positions.size() / 3);
        for (const int c : faces[f]) {
            const Vec3 p = corner(c);
            mesh.positions.insert(mesh.positions.end(), {p.x, p.y, p.z});
            mesh.normals.insert(mesh.normals.end(), {normals[f].x, normals[f].y, normals[f].z});
        }
        mesh.triangles.insert(mesh.triangles.end(),
                              {base, base + 1, base + 2, base, base + 2, base + 3});
        mesh.triangleFaceIds.insert(mesh.triangleFaceIds.end(), {f + 1, f + 1});
    }
    const std::array<std::array<int, 2>, 12> edges = {{{0, 1},
                                                       {2, 3},
                                                       {4, 5},
                                                       {6, 7},
                                                       {0, 2},
                                                       {1, 3},
                                                       {4, 6},
                                                       {5, 7},
                                                       {0, 4},
                                                       {1, 5},
                                                       {2, 6},
                                                       {3, 7}}};
    for (std::uint32_t e = 0; e < edges.size(); ++e) {
        for (const int c : edges[e]) {
            const Vec3 p = corner(c);
            mesh.edgePoints.insert(mesh.edgePoints.end(), {p.x, p.y, p.z});
        }
        mesh.edgeOffsets.push_back((e + 1) * 2);
        mesh.edgeIds.push_back(e + 1);
    }
    for (std::uint32_t v = 0; v < 8; ++v) {
        const Vec3 p = corner(static_cast<int>(v));
        mesh.vertexPositions.insert(mesh.vertexPositions.end(), {p.x, p.y, p.z});
        mesh.vertexIds.push_back(v + 1);
    }
    return mesh;
}

}  // namespace facetry::render::test
