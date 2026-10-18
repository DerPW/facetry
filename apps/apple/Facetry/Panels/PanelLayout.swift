// Where the panels are (#64): docked to the left or the right side of the window, in order, or
// floating like a small window. Collapsed panels show only their title. The arrangement belongs
// to the device, so it is kept in the user defaults. The web app follows the same rules
// (panelLayout.ts).

import Foundation
import Observation

enum PanelDock: String, Codable, Sendable {
    case left, right, float
}

struct PanelPlace: Codable, Equatable, Sendable {
    var dock: PanelDock
    /// Position in the dock, smallest first. The right dock holds the ViewCube at 0: panels with
    /// a negative order sit above it, the others below.
    var order: Int
    var collapsed = false
    /// Top-left corner while floating, in points.
    var x = 0.0
    var y = 0.0
}

/// An item in a dock as it stands on the screen: a panel, or the dock's fixed item (nil).
struct DockItem: Equatable, Sendable {
    let id: String?
    let top: Double
    let bottom: Double
}

@MainActor @Observable
final class PanelLayout {
    /// Where each panel starts, and goes back to when the layout is reset.
    static let defaults: [String: PanelPlace] = [
        "header": PanelPlace(dock: .left, order: 1),
        "parameters": PanelPlace(dock: .left, order: 2),
        "browser": PanelPlace(dock: .left, order: 3),
        "toolbar": PanelPlace(dock: .right, order: -1),
        "sketch": PanelPlace(dock: .right, order: 1),
    ]
    /// A panel dropped this close to a side of the window docks there, in points.
    static let dockReach = 56.0
    static let storageKey = "panelLayout"

    private(set) var places: [String: PanelPlace]
    @ObservationIgnored private let storage: UserDefaults

    init(storage: UserDefaults = .standard) {
        self.storage = storage
        places = Self.parse(storage.data(forKey: Self.storageKey))
    }

    /// Where a panel is; panels the layout does not know yet go to the end of the left dock.
    func place(of id: String) -> PanelPlace {
        places[id] ?? Self.defaults[id] ?? PanelPlace(dock: .left, order: 100)
    }

    /// The panels among `ids` in `dock`, in order.
    func panels(_ ids: [String], in dock: PanelDock) -> [String] {
        ids.filter { place(of: $0).dock == dock }
            .sorted { place(of: $0).order < place(of: $1).order }
    }

    /// The dock a panel dropped with the pointer at `x` goes to, or nil to float.
    static func dock(at x: Double, width: Double) -> PanelDock? {
        if x <= dockReach { return .left }
        if x >= width - dockReach { return .right }
        return nil
    }

    /// Docks panel `id` where it was dropped, at height `y`, between the items already in the
    /// dock (top to bottom), and numbers the dock's panels again.
    func drop(_ id: String, in dock: PanelDock, among items: [DockItem], at y: Double) {
        var order = items.filter { $0.id != id }
        let before = order.firstIndex { y < ($0.top + $0.bottom) / 2 } ?? order.count
        order.insert(DockItem(id: id, top: y, bottom: y), at: before)
        // Around the fixed item, which stays at 0; in a dock without one, from 1.
        let fixed = order.firstIndex { $0.id == nil }
        for (index, item) in order.enumerated() {
            guard let itemID = item.id else { continue }
            var place = place(of: itemID)
            place.dock = dock
            place.order = fixed.map { index - $0 } ?? index + 1
            places[itemID] = place
        }
        save()
    }

    /// Floats panel `id` with its top-left corner at `x`, `y`.
    func float(_ id: String, x: Double, y: Double) {
        var place = place(of: id)
        place.dock = .float
        place.x = x
        place.y = y
        places[id] = place
        save()
    }

    func toggleCollapsed(_ id: String) {
        var place = place(of: id)
        place.collapsed.toggle()
        places[id] = place
        save()
    }

    func reset() {
        places = Self.defaults
        save()
    }

    /// Keeps at least the start of a floating panel's title inside the window.
    static func clamp(x: Double, y: Double, width: Double, height: Double) -> (Double, Double) {
        (min(max(x, 0), max(width - 80, 0)), min(max(y, 0), max(height - 32, 0)))
    }

    /// The stored layout over the defaults; anything unreadable starts over from them.
    static func parse(_ data: Data?) -> [String: PanelPlace] {
        guard let data,
            let stored = try? JSONDecoder().decode([String: PanelPlace].self, from: data)
        else { return defaults }
        return defaults.merging(stored) { _, stored in stored }
    }

    private func save() {
        if let data = try? JSONEncoder().encode(places) {
            storage.set(data, forKey: Self.storageKey)
        }
    }
}
