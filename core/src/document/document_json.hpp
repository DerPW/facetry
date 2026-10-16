// JSON form of the document model, shared by save/load and the regeneration cache.
#pragma once

#include "facetry/core/document.hpp"

#include <nlohmann/json.hpp>

namespace facetry::core::json {

// Keeps keys in the order they are written, so saved documents read naturally ("id" first).
using Json = nlohmann::ordered_json;

[[nodiscard]] Json featureToJson(const Feature& feature);

}  // namespace facetry::core::json
