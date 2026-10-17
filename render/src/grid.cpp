#include "facetry/render/grid.hpp"

#include <algorithm>
#include <cmath>

namespace facetry::render {
namespace {

// Minor lines are at least this many points apart on the screen.
constexpr float kMinimumSpacing = 12.0F;

}  // namespace

GridLayout gridLayout(const Camera& camera, float unitMillimeters) {
    unitMillimeters = std::max(unitMillimeters, 1e-6F);
    const float worldPerPoint = std::max(camera.worldPerPointAtTarget(), 1e-9F);
    // The smallest 1, 2 or 5 × 10ⁿ units that is at least kMinimumSpacing points wide.
    const float wanted = kMinimumSpacing * worldPerPoint / unitMillimeters;
    const float decade = std::pow(10.0F, std::floor(std::log10(wanted)));
    const float mantissa = wanted / decade;
    const float step = mantissa <= 1.0F   ? 1.0F
                       : mantissa <= 2.0F ? 2.0F
                       : mantissa <= 5.0F ? 5.0F
                                          : 10.0F;
    GridLayout layout;
    layout.minor = step * decade * unitMillimeters;
    // The next power of ten above the minor spacing.
    layout.major = (step == 10.0F ? 100.0F : 10.0F) * decade * unitMillimeters;
    const float pixels = layout.minor / worldPerPoint;
    layout.minorOpacity =
        std::clamp((pixels - kMinimumSpacing) / kMinimumSpacing + 0.35F, 0.0F, 1.0F);
    // Wide enough to reach the horizon in typical views: a few screen widths at the target.
    const float screen = worldPerPoint * std::max(camera.viewportWidth(), camera.viewportHeight());
    layout.radius = std::max(layout.major * 4.0F, screen * 4.0F);
    return layout;
}

}  // namespace facetry::render
