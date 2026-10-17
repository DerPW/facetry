// The browser: the document as a tree of origin, bodies and sketches (Fusion's browser). The web
// app has the same in apps/web/src/components/BrowserPanel.tsx.

import SwiftUI

struct BrowserPanel: View {
    let state: SessionState
    let model: DocumentModel
    /// Opens a feature's details in the timeline.
    let selectFeature: (String) -> Void
    /// Selects the shown body in the viewport.
    let selectBody: () -> Void
    @State private var showOrigin = false
    @State private var renaming: String?
    @State private var newName = ""

    var body: some View {
        VStack(alignment: .leading, spacing: 6) {
            DisclosureGroup("Origin", isExpanded: $showOrigin) {
                ForEach(state.origin) { item in
                    HStack {
                        eye(item.visible, label: item.name) {
                            Task {
                                await model.edit([
                                    .setOriginVisible(id: item.id, visible: !item.visible)
                                ])
                            }
                        }
                        Text(item.name).foregroundStyle(.secondary)
                    }
                }
            }
            Text("Bodies").font(.caption.bold())
            ForEach(state.bodies) { body in bodyRow(body) }
            if state.bodies.isEmpty {
                Text("No bodies").foregroundStyle(.secondary)
            }
            Text("Sketches").font(.caption.bold())
            ForEach(state.features.filter { $0.type == "sketch" }) { sketch in
                HStack {
                    Button(sketch.name) { selectFeature(sketch.id) }.buttonStyle(.plain)
                    Spacer()
                    Button("Edit") { Task { await model.editSketch(sketch.id) } }
                        .foregroundStyle(.secondary)
                        .disabled(model.sketching != nil)
                }
            }
        }
        .font(.caption)
        .frame(width: 240, alignment: .leading)
    }

    private func bodyRow(_ body: SessionState.Body) -> some View {
        HStack {
            eye(body.visible, label: body.name) {
                Task { await model.edit([.setBodyVisible(id: body.id, visible: !body.visible)]) }
            }
            if renaming == body.id {
                TextField("Name", text: $newName)
                    .textFieldStyle(.roundedBorder)
                    .onSubmit {
                        let name = newName.trimmingCharacters(in: .whitespaces)
                        renaming = nil
                        if !name.isEmpty, name != body.name {
                            Task { await model.edit([.renameBody(id: body.id, name: name)]) }
                        }
                    }
            } else {
                Button(body.name) {
                    selectFeature(body.id)
                    if body.visible { selectBody() }
                }
                .buttonStyle(.plain)
                .contextMenu {
                    Button("Rename") {
                        newName = body.name
                        renaming = body.id
                    }
                    Button("Isolate") { isolate(body) }
                    Menu("Opacity") {
                        ForEach([1.0, 0.75, 0.5, 0.25], id: \.self) { opacity in
                            Button("\(Int(opacity * 100)) %") {
                                Task {
                                    await model.edit([
                                        .setBodyOpacity(id: body.id, opacity: opacity)
                                    ])
                                }
                            }
                        }
                    }
                }
            }
            Spacer()
            Button("Isolate") { isolate(body) }
                .buttonStyle(.plain)
                .foregroundStyle(.secondary)
        }
    }

    /// Shows only this body.
    private func isolate(_ body: SessionState.Body) {
        let edits = state.bodies.map {
            DocumentEdit.setBodyVisible(id: $0.id, visible: $0.id == body.id)
        }
        Task { await model.edit(edits) }
    }

    private func eye(_ visible: Bool, label: String, toggle: @escaping () -> Void) -> some View {
        Button(action: toggle) {
            Image(systemName: visible ? "eye" : "eye.slash")
                .foregroundStyle(visible ? .primary : .secondary)
        }
        .buttonStyle(.plain)
        .accessibilityLabel("\(visible ? "Hide" : "Show") \(label)")
    }
}
