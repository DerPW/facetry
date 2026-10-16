#pragma once

#include <string_view>

namespace facetry::core {

// Version of the core library, e.g. "0.1.0".
[[nodiscard]] std::string_view version() noexcept;

// Platform the core was compiled for: "macos", "ios", "ios-simulator" or "wasm".
[[nodiscard]] std::string_view platform() noexcept;

}  // namespace facetry::core
