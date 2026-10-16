#include "facetry/core/version.hpp"

#include "build_info.hpp"

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

namespace facetry::core {

std::string_view version() noexcept {
    return FACETRY_VERSION_STRING;
}

std::string_view platform() noexcept {
#if defined(__EMSCRIPTEN__)
    return "wasm";
#elif defined(__APPLE__) && TARGET_OS_SIMULATOR
    return "ios-simulator";
#elif defined(__APPLE__) && TARGET_OS_IOS
    return "ios";
#elif defined(__APPLE__) && TARGET_OS_OSX
    return "macos";
#else
    return "unknown";
#endif
}

}  // namespace facetry::core
