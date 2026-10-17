#pragma once

// Which camera move a mouse button or a scroll gesture makes, per navigation preset
// (docs/INPUT.md). Shared so that the Mac, the iPad and the web behave the same.

#include <cstdint>

namespace facetry::render {

enum class NavigationPreset : std::uint8_t {
    // Left drag orbits; right or middle drag, or Shift + left drag, pans; the wheel zooms; on a
    // trackpad two-finger scrolling orbits.
    Facetry = 0,
    // Like Fusion 360: middle drag pans, Shift + middle drag orbits, the wheel zooms, the left
    // button selects; on a trackpad two-finger scrolling pans and Shift + scrolling orbits.
    Fusion = 1,
    // For trackpads, also where a trackpad cannot be told apart from a mouse wheel (the web):
    // scrolling orbits, Shift + scrolling pans; buttons as in Facetry.
    Trackpad = 2,
};

enum class NavigationAction : std::uint8_t { None = 0, Orbit = 1, Pan = 2, Zoom = 3, Select = 4 };

// Mouse buttons numbered like the DOM's MouseEvent.button.
enum class MouseButton : std::uint8_t { Left = 0, Middle = 1, Right = 2 };

namespace modifier {
inline constexpr std::uint32_t kShift = 1U << 0U;
inline constexpr std::uint32_t kControl = 1U << 1U;
inline constexpr std::uint32_t kAlt = 1U << 2U;
inline constexpr std::uint32_t kCommand = 1U << 3U;
}  // namespace modifier

// What dragging with `button` held does. A click without moving selects with every preset.
[[nodiscard]] NavigationAction dragAction(NavigationPreset preset, MouseButton button,
                                          std::uint32_t modifiers) noexcept;

// What scrolling does. `trackpad` is true when the platform knows the scrolling comes from a
// trackpad (precise deltas on Apple), false for a mouse wheel or when it cannot tell (the web).
// Pinching always zooms.
[[nodiscard]] NavigationAction scrollAction(NavigationPreset preset, std::uint32_t modifiers,
                                            bool trackpad) noexcept;

}  // namespace facetry::render
