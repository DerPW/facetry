#include "facetry/core/naming.hpp"

#include <algorithm>
#include <cmath>

namespace facetry::core {

std::string_view toString(ElementKind kind) noexcept {
    switch (kind) {
        case ElementKind::Face:
            return "face";
        case ElementKind::Edge:
            return "edge";
        case ElementKind::Vertex:
            return "vertex";
    }
    return "element";
}

const std::vector<std::string>& NamedShape::names(ElementKind kind) const {
    switch (kind) {
        case ElementKind::Face:
            return faces;
        case ElementKind::Edge:
            return edges;
        case ElementKind::Vertex:
            break;
    }
    return vertices;
}

std::optional<std::uint32_t> NamedShape::find(ElementKind kind, std::string_view name) const {
    const std::vector<std::string>& list = names(kind);
    const auto it = std::find(list.begin(), list.end(), name);
    if (it == list.end()) {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(it - list.begin()) + 1;
}

bool sameGeometry(const ElementGeometry& a, const ElementGeometry& b) noexcept {
    // Well above numerical noise between platforms, well below anything a user would model.
    constexpr double tolerance = 1e-6;
    const double distance = std::hypot(a.centroid.x - b.centroid.x, a.centroid.y - b.centroid.y,
                                       a.centroid.z - b.centroid.z);
    const double scale = std::max({1.0, std::abs(a.size), std::abs(b.size)});
    return a.type == b.type && distance <= tolerance &&
           std::abs(a.size - b.size) <= tolerance * scale;
}

}  // namespace facetry::core
