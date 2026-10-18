// The ViewCube, laid out like Fusion's (#33, #63): clicking a face, edge or corner turns the
// view there; seen straight on a face, arrows turn it to the next face or roll it; dragging the
// cube orbits, dragging the compass ring turns around the Z axis only. Home sits at the top left,
// the named views in the menu at the bottom right. The geometry and the hit tests come from the
// shared renderer.

import SwiftUI

/// What the pointer is over in the ViewCube's square.
enum ViewCubeTarget: Equatable {
    /// A face, edge or corner cell, by the direction a click looks from.
    case cell(SIMD3<Float>)
    case arrow(ViewCubeArrow)
    case compass
}

struct ViewCubeView: View {
    /// Side of the cube's square in points.
    static let size = 124.0

    let model: ViewportModel
    @State private var hovered: ViewCubeTarget?
    @State private var lastDrag: CGSize?
    @State private var dragTarget: ViewCubeTarget?
    @Environment(\.colorScheme) private var colorScheme

    /// The named views, from the model toward the eye.
    static let namedViews: [(name: String, direction: SIMD3<Float>)] = [
        ("Top", [0, 0, 1]), ("Bottom", [0, 0, -1]), ("Front", [0, -1, 0]), ("Back", [0, 1, 0]),
        ("Left", [-1, 0, 0]), ("Right", [1, 0, 0]),
    ]

    private static let compassLabels = ["N", "E", "S", "W"]

    var body: some View {
        Canvas { context, _ in draw(in: &context) }
            .frame(width: Self.size, height: Self.size)
            .contentShape(Rectangle())
            .gesture(drag)
            .onContinuousHover { phase in
                switch phase {
                case .active(let point): hovered = model.viewCubeTarget(at: point)
                case .ended: hovered = nil
                }
            }
            .accessibilityLabel("ViewCube")
            .accessibilityHint("Turns the view to a face, edge or corner, or with the arrows")
            .overlay(alignment: .topLeading) { homeButton }
            .overlay(alignment: .bottomTrailing) { viewMenu }
            .background { shortcuts }
            .padding(8)
            .background(.regularMaterial, in: RoundedRectangle(cornerRadius: 10))
            .padding(.horizontal, 12)
    }

    private var drag: some Gesture {
        DragGesture(minimumDistance: 0)
            .onChanged { drag in
                let moved = hypot(drag.translation.width, drag.translation.height) > 4
                guard moved || lastDrag != nil else { return }
                if lastDrag == nil { dragTarget = model.viewCubeTarget(at: drag.startLocation) }
                let last = lastDrag ?? .zero
                // The compass turns around the Z axis only.
                let dy = dragTarget == .compass ? 0 : drag.translation.height - last.height
                model.orbitFromCenter(
                    dx: drag.translation.width - last.width, dy: dy, begin: lastDrag == nil)
                lastDrag = drag.translation
            }
            .onEnded { drag in
                defer {
                    lastDrag = nil
                    dragTarget = nil
                }
                guard lastDrag == nil else { return }
                switch model.viewCubeTarget(at: drag.startLocation) {
                case .cell(let direction): model.view(from: direction)
                case .arrow(let arrow): model.view(from: arrow.direction, lean: arrow.lean)
                case .compass, nil: break
                }
            }
    }

    private var homeButton: some View {
        Button {
            model.home()
        } label: {
            Image(systemName: "house")
                .font(.system(size: 13))
                .frame(width: 24, height: 24)
                .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
        .foregroundStyle(.secondary)
        .keyboardShortcut(.home, modifiers: [])
        .help("Home view (Home)")
        .accessibilityLabel("Home view")
    }

    private var viewMenu: some View {
        Menu {
            ForEach(Self.namedViews, id: \.name) { view in
                Button(view.name) { model.view(from: view.direction) }
            }
            Divider()
            Button("Fit Selection") { model.fitSelection() }
        } label: {
            Image(systemName: "chevron.down")
                .font(.system(size: 11, weight: .semibold))
                .frame(width: 24, height: 24)
                .contentShape(Rectangle())
        }
        .menuStyle(.button)
        .buttonStyle(.plain)
        .menuIndicator(.hidden)
        .fixedSize()
        .foregroundStyle(.secondary)
        .help("Views")
        .accessibilityLabel("View menu")
    }

    /// Named views on the numeric keypad, as in Blender: 1 front, 3 right, 7 top; with Control
    /// the opposite side. Shift-F fits the selection.
    private var shortcuts: some View {
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
            Button("") { model.fitSelection() }
                .keyboardShortcut("f", modifiers: .shift)
        }
        .hidden()
        .frame(width: 0, height: 0)
    }

    private func draw(in context: inout GraphicsContext) {
        let face = colorScheme == .dark ? Color(white: 0.23) : Color(white: 0.98)
        let line = Color.primary.opacity(0.2)

        // The compass under the cube.
        let compass = model.viewCubeCompass
        if !compass.ring.isEmpty {
            let active = hovered == .compass
            var ring = Path()
            ring.addLines(compass.ring)
            context.stroke(
                ring, with: .color(active ? .accentColor : line), lineWidth: active ? 2.5 : 1.5)
            for (i, label) in compass.labels.enumerated() {
                let disc = Path(
                    ellipseIn: CGRect(x: label.x - 6.5, y: label.y - 6.5, width: 13, height: 13))
                context.fill(disc, with: .color(face))
                context.stroke(disc, with: .color(active ? .accentColor : line), lineWidth: 1)
                context.draw(
                    Text(Self.compassLabels[i]).font(.system(size: 8, weight: .semibold))
                        .foregroundStyle(i == 0 ? Color.accentColor : Color.secondary),
                    at: label)
            }
        }

        for cell in model.viewCube {
            var path = Path()
            path.addLines(cell.points)
            path.closeSubpath()
            let fill = hovered == .cell(cell.direction) ? Color.accentColor : face
            context.fill(path, with: .color(fill))
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

        // Arrows around the cube: triangles that turn, curved arrows that roll.
        for arrow in model.viewCubeArrows {
            let color = hovered == .arrow(arrow) ? Color.accentColor : Color.secondary
            switch arrow.kind {
            case .turn:
                var path = Path()
                path.addLines(arrow.points)
                path.closeSubpath()
                context.fill(path, with: .color(color))
            case .rollClockwise, .rollCounterClockwise:
                drawRoll(
                    in: &context, at: arrow.center, clockwise: arrow.kind == .rollClockwise,
                    color: color)
            }
        }
    }

    /// A curved arrow around `center` that rolls the view, as on Fusion's ViewCube.
    private func drawRoll(
        in context: inout GraphicsContext, at center: CGPoint, clockwise: Bool, color: Color
    ) {
        let radius = Self.size * 0.042
        // Three quarters of a circle; the arrowhead at the end points along the turn. Angles grow
        // clockwise on the screen, which Path calls counter-clockwise.
        let start = clockwise ? -0.9 * Double.pi : -0.1 * Double.pi
        let end = clockwise ? start + 1.5 * .pi : start - 1.5 * .pi
        var arc = Path()
        arc.addArc(
            center: center, radius: radius, startAngle: .radians(start), endAngle: .radians(end),
            clockwise: !clockwise)
        context.stroke(arc, with: .color(color), lineWidth: 2)
        let tip = CGPoint(x: center.x + radius * cos(end), y: center.y + radius * sin(end))
        let sign = clockwise ? 1.0 : -1.0
        let (tx, ty) = (-sin(end) * sign, cos(end) * sign)  // along the turn
        let head = 4.5
        var arrowhead = Path()
        arrowhead.addLines([
            CGPoint(x: tip.x + tx * head, y: tip.y + ty * head),
            CGPoint(x: tip.x - ty * head * 0.8, y: tip.y + tx * head * 0.8),
            CGPoint(x: tip.x + ty * head * 0.8, y: tip.y - tx * head * 0.8),
        ])
        arrowhead.closeSubpath()
        context.fill(arrowhead, with: .color(color))
    }
}
