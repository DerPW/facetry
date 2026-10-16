#include "facetry/core/stl.hpp"

#include "facetry/core/error.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

namespace facetry::core {
namespace {

void writeBytes(std::ostream& out, const void* data, std::size_t size) {
    out.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
}

void writeFloat(std::ostream& out, float value) {
    static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559);
    writeBytes(out, &value, sizeof value);  // STL is little-endian, like all supported targets
}

}  // namespace

void writeBinaryStl(const Mesh& mesh, std::ostream& out, std::string_view header) {
    std::array<char, 80> headerBytes{};
    std::memcpy(headerBytes.data(), header.data(), std::min(header.size(), headerBytes.size()));
    writeBytes(out, headerBytes.data(), headerBytes.size());

    const auto count = static_cast<std::uint32_t>(mesh.triangleCount());
    writeBytes(out, &count, sizeof count);

    const auto point = [&](std::uint32_t index, std::size_t axis) {
        return mesh.positions[std::size_t{index} * 3 + axis];
    };
    for (std::size_t t = 0; t < mesh.triangleCount(); ++t) {
        const std::uint32_t a = mesh.triangles[t * 3];
        const std::uint32_t b = mesh.triangles[t * 3 + 1];
        const std::uint32_t c = mesh.triangles[t * 3 + 2];
        // Facet normal from the winding; slicers recompute it anyway.
        const float ux = point(b, 0) - point(a, 0);
        const float uy = point(b, 1) - point(a, 1);
        const float uz = point(b, 2) - point(a, 2);
        const float vx = point(c, 0) - point(a, 0);
        const float vy = point(c, 1) - point(a, 1);
        const float vz = point(c, 2) - point(a, 2);
        float nx = uy * vz - uz * vy;
        float ny = uz * vx - ux * vz;
        float nz = ux * vy - uy * vx;
        const float length = std::sqrt(nx * nx + ny * ny + nz * nz);
        if (length > 0.0F) {
            nx /= length;
            ny /= length;
            nz /= length;
        }
        writeFloat(out, nx);
        writeFloat(out, ny);
        writeFloat(out, nz);
        for (const std::uint32_t index : {a, b, c}) {
            writeFloat(out, point(index, 0));
            writeFloat(out, point(index, 1));
            writeFloat(out, point(index, 2));
        }
        const std::uint16_t attributes = 0;
        writeBytes(out, &attributes, sizeof attributes);
    }
    if (!out) {
        throw Error(ErrorCode::Io, "Writing STL failed");
    }
}

}  // namespace facetry::core
