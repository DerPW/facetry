#pragma once

#include <cstddef>
#include <memory>
#include <utility>

namespace facetry::core {

struct Vec3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

struct BoundingBox {
    Vec3 min;
    Vec3 max;
};

struct TopologyCounts {
    std::size_t solids = 0;
    std::size_t faces = 0;
    std::size_t edges = 0;
    std::size_t vertices = 0;
};

struct ShapeProperties {
    double volume = 0.0;
    double area = 0.0;
    BoundingBox bounds;
    TopologyCounts counts;
};

// An immutable solid or compound from the geometry kernel. Cheap to copy: copies share the same
// geometry. Faces, edges and vertices are numbered from 1; the numbers are only valid for this
// shape and change with every modeling operation.
class Shape {
  public:
    class Impl;  // Defined by the kernel adapter; OpenCASCADE types stay behind it.

    Shape() = default;
    explicit Shape(std::shared_ptr<const Impl> impl) : impl_(std::move(impl)) {}

    [[nodiscard]] bool isNull() const noexcept {
        return impl_ == nullptr;
    }
    [[nodiscard]] TopologyCounts topologyCounts() const;
    [[nodiscard]] ShapeProperties properties() const;

    // Throws Error(InvalidArgument) for a null shape.
    [[nodiscard]] const Impl& impl() const;

  private:
    std::shared_ptr<const Impl> impl_;
};

}  // namespace facetry::core
