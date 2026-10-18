import Foundation
import Testing

@testable import Facetry

@MainActor
struct PanelLayoutTests {
    /// A layout on its own defaults, so the tests leave the app's arrangement alone.
    private func freshLayout() -> (PanelLayout, UserDefaults) {
        let suite = "PanelLayoutTests.\(UUID().uuidString)"
        let storage = UserDefaults(suiteName: suite) ?? .standard
        storage.removePersistentDomain(forName: suite)
        return (PanelLayout(storage: storage), storage)
    }

    @Test func docksNearTheSidesAndFloatsInBetween() {
        #expect(PanelLayout.dock(at: 20, width: 1000) == .left)
        #expect(PanelLayout.dock(at: 990, width: 1000) == .right)
        #expect(PanelLayout.dock(at: 500, width: 1000) == nil)
    }

    @Test func docksAPanelBetweenTheItemsWhereItIsDropped() {
        let (layout, _) = freshLayout()
        let left = [
            DockItem(id: "header", top: 0, bottom: 100),
            DockItem(id: "parameters", top: 110, bottom: 300),
        ]
        layout.drop("toolbar", in: .left, among: left, at: 120)
        #expect(layout.place(of: "header").order == 1)
        #expect(layout.place(of: "toolbar") == PanelPlace(dock: .left, order: 2))
        #expect(layout.place(of: "parameters").order == 3)
        #expect(
            layout.panels(["parameters", "toolbar", "header"], in: .left)
                == ["header", "toolbar", "parameters"])
    }

    @Test func keepsTheViewCubeAtZeroInTheRightDock() {
        let (layout, _) = freshLayout()
        let right = [
            DockItem(id: "toolbar", top: 0, bottom: 40),
            DockItem(id: nil, top: 50, bottom: 190),
        ]
        layout.drop("browser", in: .right, among: right, at: 10)
        #expect(layout.place(of: "browser").order == -2)
        #expect(layout.place(of: "toolbar").order == -1)
        layout.drop("browser", in: .right, among: right, at: 400)
        #expect(layout.place(of: "browser").order == 1)
    }

    @Test func floatsCollapsesAndKeepsTheArrangement() {
        let (layout, storage) = freshLayout()
        layout.float("browser", x: 300, y: 200)
        layout.toggleCollapsed("browser")
        let reloaded = PanelLayout(storage: storage)
        #expect(
            reloaded.place(of: "browser")
                == PanelPlace(dock: .float, order: 3, collapsed: true, x: 300, y: 200))
        reloaded.reset()
        #expect(
            PanelLayout(storage: storage).place(of: "browser") == PanelLayout.defaults["browser"])
        #expect(PanelLayout.clamp(x: 2000, y: -10, width: 1000, height: 800) == (920, 0))
    }

    @Test func readsAStoredLayoutOverTheDefaults() {
        #expect(PanelLayout.parse(Data("not json".utf8)) == PanelLayout.defaults)
        let stored = Data(
            #"{"browser":{"dock":"float","order":0,"collapsed":true,"x":1,"y":2}}"#.utf8)
        let places = PanelLayout.parse(stored)
        #expect(places["browser"]?.dock == .float)
        #expect(places["parameters"] == PanelLayout.defaults["parameters"])
    }
}
