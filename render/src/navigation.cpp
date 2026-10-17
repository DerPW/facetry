#include "facetry/render/navigation.hpp"

namespace facetry::render {

NavigationAction dragAction(NavigationPreset preset, MouseButton button,
                            std::uint32_t modifiers) noexcept {
    const bool shift = (modifiers & modifier::kShift) != 0;
    if (preset == NavigationPreset::Fusion) {
        switch (button) {
            case MouseButton::Left:
                return NavigationAction::Select;
            case MouseButton::Middle:
                return shift ? NavigationAction::Orbit : NavigationAction::Pan;
            case MouseButton::Right:
                return NavigationAction::None;  // the context menu, later
        }
        return NavigationAction::None;
    }
    if (button == MouseButton::Left) {
        return shift ? NavigationAction::Pan : NavigationAction::Orbit;
    }
    return NavigationAction::Pan;
}

NavigationAction scrollAction(NavigationPreset preset, std::uint32_t modifiers,
                              bool trackpad) noexcept {
    const bool shift = (modifiers & modifier::kShift) != 0;
    switch (preset) {
        case NavigationPreset::Facetry:
            if (shift) {
                return NavigationAction::Pan;
            }
            return trackpad ? NavigationAction::Orbit : NavigationAction::Zoom;
        case NavigationPreset::Fusion:
            if (!trackpad) {
                return NavigationAction::Zoom;
            }
            return shift ? NavigationAction::Orbit : NavigationAction::Pan;
        case NavigationPreset::Trackpad:
            return shift ? NavigationAction::Pan : NavigationAction::Orbit;
    }
    return NavigationAction::Zoom;
}

}  // namespace facetry::render
