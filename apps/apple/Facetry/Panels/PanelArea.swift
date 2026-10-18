// The panels, docked to the left or the right side or floating (#64). Dragging a title moves a
// panel; dropped near a side it docks there, between the panels where it is dropped. The chevron
// collapses a panel to its title.

import SwiftUI

struct PanelSpec: Identifiable {
    let id: String
    let title: String
    let content: AnyView

    init(_ id: String, _ title: String, @ViewBuilder content: () -> some View) {
        self.id = id
        self.title = title
        self.content = AnyView(content())
    }
}

/// The coordinate space of the panel area, for frames and drags.
private let panelSpace = "panels"

/// A title being dragged: where it was grabbed and where the pointer is, in the panel area.
private struct PanelDrag: Equatable {
    let id: String
    /// From the panel's top-left corner to where the title was grabbed.
    let grab: CGSize
    var translation: CGSize
    var location: CGPoint
    var target: PanelDock?
}

struct PanelArea<Fixed: View>: View {
    let layout: PanelLayout
    let panels: [PanelSpec]
    /// What the right dock holds at order 0 (the ViewCube).
    @ViewBuilder let rightFixed: Fixed

    @State private var drag: PanelDrag?
    /// The panels' and the fixed item's frames at rest, in the panel area.
    @State private var frames: [String: CGRect] = [:]
    @State private var fixedFrame = CGRect.zero

    var body: some View {
        GeometryReader { geometry in
            let ids = panels.map(\.id)
            let right = layout.panels(ids, in: .right)
            ZStack(alignment: .topLeading) {
                VStack(alignment: .leading, spacing: 0) {
                    ForEach(layout.panels(ids, in: .left), id: \.self) { frame($0, in: geometry) }
                }
                .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)
                .zIndex(dragging(in: .left) ? 1 : 0)
                VStack(alignment: .trailing, spacing: 0) {
                    ForEach(right.filter { layout.place(of: $0).order < 0 }, id: \.self) {
                        frame($0, in: geometry)
                    }
                    rightFixed
                        .onGeometryChange(for: CGRect.self) {
                            $0.frame(in: .named(panelSpace))
                        } action: {
                            fixedFrame = $0
                        }
                    ForEach(right.filter { layout.place(of: $0).order >= 0 }, id: \.self) {
                        frame($0, in: geometry)
                    }
                }
                .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topTrailing)
                .zIndex(dragging(in: .right) ? 1 : 0)
                ForEach(layout.panels(ids, in: .float), id: \.self) { id in
                    let place = layout.place(of: id)
                    let (x, y) = PanelLayout.clamp(
                        x: place.x, y: place.y, width: geometry.size.width,
                        height: geometry.size.height)
                    frame(id, in: geometry)
                        .offset(x: x, y: y)
                        .zIndex(drag?.id == id ? 2 : 1)
                }
                if let target = drag?.target {
                    // Where the panel docks if it is dropped now.
                    Capsule()
                        .fill(Color.accentColor.opacity(0.6))
                        .frame(width: 6)
                        .padding(.vertical, 8)
                        .frame(
                            maxWidth: .infinity, maxHeight: .infinity,
                            alignment: target == .left ? .leading : .trailing
                        )
                        .allowsHitTesting(false)
                        .zIndex(3)
                }
            }
            .coordinateSpace(.named(panelSpace))
        }
    }

    private func dragging(in dock: PanelDock) -> Bool {
        drag.map { layout.place(of: $0.id).dock == dock } ?? false
    }

    @ViewBuilder
    private func frame(_ id: String, in geometry: GeometryProxy) -> some View {
        if let panel = panels.first(where: { $0.id == id }) {
            let place = layout.place(of: id)
            VStack(alignment: .leading, spacing: 6) {
                title(panel, collapsed: place.collapsed, in: geometry)
                if place.collapsed {
                    // Out of sight, but its keyboard shortcuts keep working.
                    panel.content.hidden().frame(width: 0, height: 0).clipped()
                        .accessibilityHidden(true)
                } else {
                    panel.content
                }
            }
            .padding(.horizontal, 12)
            .padding(.top, 6)
            .padding(.bottom, place.collapsed ? 6 : 10)
            // As wide as the content, not the dock.
            .fixedSize(horizontal: true, vertical: false)
            .background(.regularMaterial, in: RoundedRectangle(cornerRadius: 10))
            .shadow(color: .black.opacity(drag?.id == id ? 0.18 : 0), radius: 12, y: 6)
            .onGeometryChange(for: CGRect.self) {
                $0.frame(in: .named(panelSpace))
            } action: {
                if drag?.id != id { frames[id] = $0 }
            }
            .offset(drag?.id == id ? drag?.translation ?? .zero : .zero)
            .zIndex(drag?.id == id ? 1 : 0)
            .padding(.horizontal, 12)
            .padding(.vertical, 6)
            .accessibilityElement(children: .contain)
            .accessibilityLabel(panel.title)
        }
    }

    private func title(_ panel: PanelSpec, collapsed: Bool, in geometry: GeometryProxy)
        -> some View
    {
        HStack(spacing: 6) {
            Image(systemName: "line.3.horizontal")
                .font(.system(size: 10, weight: .semibold))
                .foregroundStyle(.tertiary)
            Text(panel.title)
                .font(.caption.bold())
                .foregroundStyle(.secondary)
                .lineLimit(1)
            Spacer(minLength: 8)
            Button {
                layout.toggleCollapsed(panel.id)
            } label: {
                Image(systemName: collapsed ? "chevron.right" : "chevron.down")
                    .font(.system(size: 10, weight: .semibold))
                    .frame(width: 22, height: 22)
                    .contentShape(Rectangle())
            }
            .buttonStyle(.plain)
            .foregroundStyle(.secondary)
            .help(collapsed ? "Show" : "Collapse")
            .accessibilityLabel(collapsed ? "Show \(panel.title)" : "Collapse \(panel.title)")
        }
        .contentShape(Rectangle())
        .gesture(move(panel.id, in: geometry))
    }

    /// Dragging the title: the panel follows the pointer and lands where it is dropped. It stays
    /// in its place in the view tree until then, so the gesture is not cut off.
    private func move(_ id: String, in geometry: GeometryProxy) -> some Gesture {
        DragGesture(minimumDistance: 4, coordinateSpace: .named(panelSpace))
            .onChanged { value in
                let target = PanelLayout.dock(at: value.location.x, width: geometry.size.width)
                if var current = drag, current.id == id {
                    current.translation = value.translation
                    current.location = value.location
                    current.target = target
                    drag = current
                } else {
                    let origin = frames[id]?.origin ?? value.startLocation
                    drag = PanelDrag(
                        id: id,
                        grab: CGSize(
                            width: value.startLocation.x - origin.x,
                            height: value.startLocation.y - origin.y),
                        translation: value.translation, location: value.location, target: target)
                }
            }
            .onEnded { value in
                guard let current = drag, current.id == id else { return }
                drag = nil
                switch current.target {
                case .left?, .right?:
                    let dock = current.target ?? .left
                    var items = layout.panels(panels.map(\.id), in: dock)
                        .filter { $0 != id }
                        .compactMap { other in
                            frames[other].map {
                                DockItem(id: other, top: $0.minY, bottom: $0.maxY)
                            }
                        }
                    if dock == .right {
                        items.append(
                            DockItem(id: nil, top: fixedFrame.minY, bottom: fixedFrame.maxY))
                    }
                    items.sort { $0.top < $1.top }
                    layout.drop(id, in: dock, among: items, at: value.location.y)
                default:
                    // The panel frame's outer padding keeps it off the corner it is placed at.
                    let (x, y) = PanelLayout.clamp(
                        x: value.location.x - current.grab.width - 12,
                        y: value.location.y - current.grab.height - 6,
                        width: geometry.size.width, height: geometry.size.height)
                    layout.float(id, x: x, y: y)
                }
            }
    }
}
