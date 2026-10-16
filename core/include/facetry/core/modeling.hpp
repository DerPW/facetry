#pragma once

#include "facetry/core/shape.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace facetry::core {

// Box with one corner at the origin.
[[nodiscard]] Shape makeBox(double dx, double dy, double dz);

// Cylinder standing on the XY plane, centered at (x, y).
[[nodiscard]] Shape makeCylinder(double x, double y, double radius, double height);

enum class BooleanOp { Fuse = 0, Cut = 1, Common = 2 };

[[nodiscard]] Shape booleanOp(BooleanOp op, const Shape& shape, const Shape& tool);

// Rounds the given edges (1-based edge IDs of `shape`) with a constant radius.
[[nodiscard]] Shape fillet(const Shape& shape, std::span<const std::uint32_t> edgeIds,
                           double radius);

// IDs of all straight edges parallel to `direction`.
[[nodiscard]] std::vector<std::uint32_t> straightEdgesParallelTo(const Shape& shape,
                                                                 Vec3 direction);

}  // namespace facetry::core
