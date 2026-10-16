#pragma once

#include "facetry/core/shape.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace facetry::script {

// Builds a shape from a JSON model description: a list of operations and the ID of the result.
// The format is documented in tests/models/README.md. Throws core::Error(InvalidArgument) for
// malformed input and passes on kernel errors.
[[nodiscard]] core::Shape buildModel(std::string_view json);

// Compares a shape with the "expected" section of a model description. Returns one message per
// mismatch; an empty list means the shape matches. Throws if the description has no
// "expected" section.
[[nodiscard]] std::vector<std::string> checkExpected(const core::Shape& shape,
                                                     std::string_view json);

}  // namespace facetry::script
