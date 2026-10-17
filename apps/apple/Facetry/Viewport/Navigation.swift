// Mouse and trackpad presets (docs/INPUT.md). Which button or scroll gesture does what is shared
// C++ (facetry_render.h), so the Mac, the iPad and the web behave the same.

import CFacetryCore
import SwiftUI

enum NavigationPreset: Int, CaseIterable, Identifiable, Sendable {
    /// Left drag orbits; right or middle drag, or Shift + left drag, pans; the wheel zooms.
    case facetry = 0
    /// Like Fusion 360: middle drag pans, Shift + middle drag orbits, the left button selects.
    case fusion = 1
    /// Scrolling orbits and Shift + scrolling pans, also with a mouse wheel.
    case trackpad = 2

    var id: Int { rawValue }

    var title: String {
        switch self {
        case .facetry: "Facetry"
        case .fusion: "Fusion"
        case .trackpad: "Trackpad"
        }
    }

    enum Action: UInt32 {
        case none = 0
        case orbit = 1
        case pan = 2
        case zoom = 3
        /// A click selects; a drag will draw a selection box.
        case select = 4
    }

    /// Numbered like the DOM's MouseEvent.button.
    enum Button: UInt32 {
        case left = 0
        case middle = 1
        case right = 2
    }

    func drag(_ button: Button, modifiers: UInt32) -> Action {
        Action(rawValue: facetry_navigation_drag(UInt32(rawValue), button.rawValue, modifiers))
            ?? .none
    }

    /// `trackpad`: the scrolling comes from a trackpad (precise deltas), not a mouse wheel.
    func scroll(modifiers: UInt32, trackpad: Bool) -> Action {
        Action(
            rawValue: facetry_navigation_scroll(UInt32(rawValue), modifiers, trackpad ? 1 : 0))
            ?? .zoom
    }
}

#if os(macOS)
    extension NSEvent.ModifierFlags {
        /// facetry_modifier mask.
        var navigationMask: UInt32 {
            (contains(.shift) ? 1 : 0) | (contains(.control) ? 2 : 0) | (contains(.option) ? 4 : 0)
                | (contains(.command) ? 8 : 0)
        }
    }
#else
    extension UIKeyModifierFlags {
        /// facetry_modifier mask.
        var navigationMask: UInt32 {
            (contains(.shift) ? 1 : 0) | (contains(.control) ? 2 : 0)
                | (contains(.alternate) ? 4 : 0) | (contains(.command) ? 8 : 0)
        }
    }
#endif
