#pragma once

// The ground grid on the XY plane (#34): its spacing follows the zoom level in the document's
// units, so it stays readable from a 1 mm part to a 1 m part.

#include "facetry/render/camera.hpp"

namespace facetry::render {

struct GridLayout {
    // Distance between minor lines, and between major lines, in millimeters (world units).
    float minor = 1.0F;
    float major = 10.0F;
    // Minor lines fade in as they move apart on the screen: 0 hidden, 1 fully drawn.
    float minorOpacity = 1.0F;
    // Half the side of the drawn square around the target; the grid fades out toward its edge.
    float radius = 100.0F;
};

// Minor lines are 1, 2 or 5 × 10ⁿ document units apart, at least kMinimumSpacing points on the
// screen at the target; major lines fall on the next power of ten. `unitMillimeters` is the size
// of one document unit (25.4 for inches).
[[nodiscard]] GridLayout gridLayout(const Camera& camera, float unitMillimeters);

}  // namespace facetry::render
