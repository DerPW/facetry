#include "facetry/render/navigation.hpp"

#include <doctest.h>

using namespace facetry::render;

namespace {

constexpr std::uint32_t kNone = 0;
constexpr std::uint32_t kShift = modifier::kShift;

}  // namespace

TEST_CASE("Facetry preset: left drag orbits, the other buttons pan, the wheel zooms") {
    const auto preset = NavigationPreset::Facetry;
    CHECK(dragAction(preset, MouseButton::Left, kNone) == NavigationAction::Orbit);
    CHECK(dragAction(preset, MouseButton::Left, kShift) == NavigationAction::Pan);
    CHECK(dragAction(preset, MouseButton::Middle, kNone) == NavigationAction::Pan);
    CHECK(dragAction(preset, MouseButton::Right, kNone) == NavigationAction::Pan);
    CHECK(scrollAction(preset, kNone, false) == NavigationAction::Zoom);
    CHECK(scrollAction(preset, kShift, false) == NavigationAction::Pan);
    CHECK(scrollAction(preset, kNone, true) == NavigationAction::Orbit);
    CHECK(scrollAction(preset, kShift, true) == NavigationAction::Pan);
}

TEST_CASE("Fusion preset: middle drag pans, Shift + middle drag orbits, left selects") {
    const auto preset = NavigationPreset::Fusion;
    CHECK(dragAction(preset, MouseButton::Middle, kNone) == NavigationAction::Pan);
    CHECK(dragAction(preset, MouseButton::Middle, kShift) == NavigationAction::Orbit);
    CHECK(dragAction(preset, MouseButton::Left, kNone) == NavigationAction::Select);
    CHECK(dragAction(preset, MouseButton::Left, kShift) == NavigationAction::Select);
    CHECK(dragAction(preset, MouseButton::Right, kNone) == NavigationAction::None);
    CHECK(scrollAction(preset, kNone, false) == NavigationAction::Zoom);
    CHECK(scrollAction(preset, kShift, false) == NavigationAction::Zoom);
    // Fusion's trackpad gestures: two fingers pan, with Shift they orbit.
    CHECK(scrollAction(preset, kNone, true) == NavigationAction::Pan);
    CHECK(scrollAction(preset, kShift, true) == NavigationAction::Orbit);
}

TEST_CASE("Trackpad preset: scrolling orbits even when the platform cannot tell") {
    const auto preset = NavigationPreset::Trackpad;
    for (const bool trackpad : {false, true}) {
        CHECK(scrollAction(preset, kNone, trackpad) == NavigationAction::Orbit);
        CHECK(scrollAction(preset, kShift, trackpad) == NavigationAction::Pan);
    }
    CHECK(dragAction(preset, MouseButton::Left, kNone) == NavigationAction::Orbit);
    CHECK(dragAction(preset, MouseButton::Right, kNone) == NavigationAction::Pan);
}
