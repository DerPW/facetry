import SwiftUI

/// The sketch tools and actions as a vertical palette of icons (#62), shown under the ViewCube
/// while a sketch is edited.
struct SketchPalette: View {
    let model: DocumentModel
    let name: String

    private let columns = Array(repeating: GridItem(.fixed(40), spacing: 4), count: 3)

    var body: some View {
        VStack(alignment: .leading, spacing: 6) {
            Text(name).font(.caption.bold()).lineLimit(1)
            group {
                tool(nil)
                ForEach(SketchToolKind.allCases.filter { !$0.isEditing }) { tool($0) }
            }
            group {
                ForEach(SketchToolKind.allCases.filter(\.isEditing)) { tool($0) }
            }
            group {
                icon(
                    "arrow.down.to.line",
                    "Project the selected faces and edges; without a selection, the origin and axes"
                ) { Task { await model.projectSelection() } }
                icon(
                    "ruler", "Construction curves (X): new ones, or the selected ones",
                    active: model.construction
                ) { Task { await model.toggleConstruction() } }
                icon("grid", "Snap free points to the grid", active: model.gridSnap) {
                    model.gridSnap.toggle()
                }
                icon("trash", "Delete the selected points and curves (Delete)") {
                    Task { await model.deleteSelected() }
                }
                .disabled(model.tool != nil || model.selectedSketchEntities.isEmpty)
            }
            group {
                icon("checkmark", "Finish sketch") { Task { await model.finishSketch(keep: true) } }
                icon("xmark", "Cancel the sketch") {
                    Task { await model.finishSketch(keep: false) }
                }
            }
        }
        .frame(width: 132)
        .background { shortcuts }
    }

    /// A group of icons in three columns, under a divider.
    private func group(@ViewBuilder _ content: () -> some View) -> some View {
        VStack(spacing: 6) {
            Divider()
            LazyVGrid(columns: columns, alignment: .leading, spacing: 4, content: content)
        }
    }

    private func tool(_ kind: SketchToolKind?) -> some View {
        var title = kind?.title ?? "Select"
        if let key = kind?.key {
            title += " (\(String(key).uppercased()))"
        }
        return icon(kind?.symbol ?? "cursorarrow", title, active: model.tool == kind) {
            model.tool = kind
        }
    }

    private func icon(
        _ symbol: String, _ title: String, active: Bool = false, action: @escaping () -> Void
    ) -> some View {
        Button(action: action) {
            Image(systemName: symbol)
                .font(.system(size: 17))
                .frame(width: 40, height: 40)
                .foregroundStyle(active ? Color.white : Color.primary)
                .background(
                    active ? Color.accentColor.opacity(0.85) : Color.clear,
                    in: RoundedRectangle(cornerRadius: 8)
                )
                .contentShape(RoundedRectangle(cornerRadius: 8))
        }
        .buttonStyle(.plain)
        .help(title)
        .accessibilityLabel(title)
    }

    /// Keys choose tools and toggle construction while sketching (docs/INPUT.md); Delete deletes.
    private var shortcuts: some View {
        ZStack {
            ForEach(SketchToolKind.allCases.filter { $0.key != nil }) { kind in
                Button(kind.title) { model.tool = kind }
                    .keyboardShortcut(KeyEquivalent(kind.key ?? " "), modifiers: [])
            }
            Button("Construction") { Task { await model.toggleConstruction() } }
                .keyboardShortcut("x", modifiers: [])
            Button("Delete") { Task { await model.deleteSelected() } }
                .keyboardShortcut(.delete, modifiers: [])
                .disabled(model.tool != nil || model.selectedSketchEntities.isEmpty)
        }
        .hidden()
        .frame(width: 0, height: 0)
    }
}

extension SketchToolKind {
    /// The SF Symbol that shows the tool in the sketch palette.
    var symbol: String {
        switch self {
        case .line: "line.diagonal"
        case .rectangleTwoPoint: "rectangle"
        case .rectangleThreePoint: "rhombus"
        case .rectangleCenter: "rectangle.center.inset.filled"
        case .circleCenter: "circle.circle"
        case .circleTwoPoint: "circle.and.line.horizontal"
        case .circleThreePoint: "circle.dotted"
        case .arcThreePoint: "point.topleft.down.curvedto.point.bottomright.up"
        case .arcCenter: "point.topleft.down.to.point.bottomright.curvepath"
        case .arcTangent: "point.bottomleft.forward.to.point.topright.scurvepath"
        case .point: "smallcircle.filled.circle"
        case .trim: "scissors"
        case .extend: "arrow.right.to.line"
        case .fillet: "rectangle.roundedtop"
        case .move: "arrow.up.and.down.and.arrow.left.and.right"
        case .copy: "square.on.square"
        }
    }
}
