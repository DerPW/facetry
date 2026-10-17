// The ViewCube (#33): shows the camera's orientation. Clicking a face, edge or corner turns the
// view there; dragging it orbits. The cells and the hit test come from the shared renderer.

import SwiftUI

struct ViewCubeView: View {
    /// Side of the cube's square in points.
    static let size = 110.0

    let model: ViewportModel
    @State private var hovered: SIMD3<Float>?
    @State private var lastDrag: CGSize?
    @Environment(\.colorScheme) private var colorScheme

    /// The named views, from the model toward the eye.
    static let namedViews: [(name: String, direction: SIMD3<Float>)] = [
        ("Top", [0, 0, 1]), ("Bottom", [0, 0, -1]), ("Front", [0, -1, 0]), ("Back", [0, 1, 0]),
        ("Left", [-1, 0, 0]), ("Right", [1, 0, 0]),
    ]

    var body: some View {
        VStack(spacing: 6) {
            Canvas { context, _ in draw(in: &context) }
                .frame(width: Self.size, height: Self.size)
                .contentShape(Rectangle())
                .gesture(
                    DragGesture(minimumDistance: 0)
                        .onChanged { drag in
                            let moved = hypot(drag.translation.width, drag.translation.height) > 4
                            guard moved || lastDrag != nil else { return }
                            let last = lastDrag ?? .zero
                            model.orbitFromCenter(
                                dx: drag.translation.width - last.width,
                                dy: drag.translation.height - last.height, begin: lastDrag == nil)
                            lastDrag = drag.translation
                        }
                        .onEnded { drag in
                            defer { lastDrag = nil }
                            guard lastDrag == nil,
                                let cell = model.viewCubeCell(at: drag.startLocation)
                            else { return }
                            model.view(from: cell.direction)
                        }
                )
                .onContinuousHover { phase in
                    switch phase {
                    case .active(let point): hovered = model.viewCubeCell(at: point)?.direction
                    case .ended: hovered = nil
                    }
                }
                .accessibilityLabel("ViewCube")
                .accessibilityHint("Turns the view to a face, edge or corner")
            HStack(spacing: 6) {
                Button("Home") { model.home() }
                    .keyboardShortcut(.home, modifiers: [])
                Menu("View") {
                    ForEach(Self.namedViews, id: \.name) { view in
                        Button(view.name) { model.view(from: view.direction) }
                    }
                }
                .fixedSize()
                Button("Fit sel.") { model.fitSelection() }
                    .keyboardShortcut("f", modifiers: .shift)
            }
            .font(.caption)
            .controlSize(.small)
            namedViewShortcuts
        }
        .padding(8)
        .background(.regularMaterial, in: RoundedRectangle(cornerRadius: 10))
        .padding(.horizontal, 12)
    }

    /// Named views on the numeric keypad, as in Blender: 1 front, 3 right, 7 top; with Control
    /// the opposite side.
    private var namedViewShortcuts: some View {
        let keys: [(KeyEquivalent, SIMD3<Float>)] = [
            ("1", [0, -1, 0]), ("3", [1, 0, 0]), ("7", [0, 0, 1]),
        ]
        return ZStack {
            ForEach(keys.indices, id: \.self) { i in
                let (key, direction) = keys[i]
                Button("") { model.view(from: direction) }
                    .keyboardShortcut(key, modifiers: .numericPad)
                Button("") { model.view(from: -direction) }
                    .keyboardShortcut(key, modifiers: [.numericPad, .control])
            }
        }
        .hidden()
        .frame(width: 0, height: 0)
    }

    private func draw(in context: inout GraphicsContext) {
        let face = colorScheme == .dark ? Color(white: 0.23) : Color(white: 0.98)
        let line = Color.primary.opacity(0.2)
        for cell in model.viewCube {
            var path = Path()
            path.addLines(cell.points)
            path.closeSubpath()
            context.fill(path, with: .color(cell.direction == hovered ? .accentColor : face))
            context.stroke(path, with: .color(line), lineWidth: 0.5)
        }
        // Face names on the center cells, sized to the cell; faces seen edge-on stay unlabeled.
        for cell in model.viewCube where cell.region == 0 && cell.points.count == 4 {
            let p = cell.points
            let area =
                abs((p[2].x - p[0].x) * (p[3].y - p[1].y) - (p[3].x - p[1].x) * (p[2].y - p[0].y))
                / 2
            let fontSize = min(12, area.squareRoot() * 0.3)
            guard fontSize >= 6 else { continue }
            let center = CGPoint(
                x: (p[0].x + p[1].x + p[2].x + p[3].x) / 4,
                y: (p[0].y + p[1].y + p[2].y + p[3].y) / 4)
            let name = ViewCubeCell.faceNames[cell.face].uppercased()
            context.draw(
                Text(name).font(.system(size: fontSize, weight: .semibold))
                    .foregroundStyle(.secondary),
                at: center)
        }
    }
}
