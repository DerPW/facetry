#pragma once

#include "facetry/core/mesh.hpp"

#include <ostream>
#include <string_view>

namespace facetry::core {

// Writes the triangles of `mesh` as binary STL. Throws Error(Io) if the stream fails.
void writeBinaryStl(const Mesh& mesh, std::ostream& out, std::string_view header = "Facetry");

}  // namespace facetry::core
