import SwiftUI

struct ContentView: View {
    @Binding var document: FacetryDocument
    @State private var viewport = ViewportModel()
    @State private var model: DocumentModel?

    private var version: String {
        Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String ?? "?"
    }

    var body: some View {
        ViewportView(model: viewport)
            .ignoresSafeArea()
            .overlay(alignment: .topLeading) {
                VStack(alignment: .leading, spacing: 0) {
                    header
                    if let model, let state = model.state {
                        ModelPanel(state: state, model: model)
                    }
                }
            }
            .overlay(alignment: .topTrailing) { controls }
            .overlay(alignment: .bottomLeading) { stats }
            .overlay(alignment: .bottomTrailing) { selection }
            .task {
                let model = DocumentModel(viewport: viewport)
                self.model = model
                await model.load()
            }
    }

    private var header: some View {
        VStack(alignment: .leading, spacing: 2) {
            Text("Facetry").font(.headline)
            Text(
                "Version \(version) · tech preview\(model?.stress == true ? " · stress test" : "")")
            Text(viewport.error ?? model?.error ?? coreStatus)
                .foregroundStyle(model?.error != nil ? .red : .secondary)
        }
        .font(.caption)
        .foregroundStyle(.secondary)
        .frame(maxWidth: 320, alignment: .leading)
        .panel()
    }

    private var coreStatus: String {
        guard model?.ready == true else { return "Starting the core…" }
        let versions = CoreVersions.current
        return "Core \(versions.core) · OpenCASCADE \(versions.kernel) · \(versions.platform)"
    }

    private var controls: some View {
        HStack {
            Button("Undo") { Task { await model?.undo() } }
                .disabled(model?.state?.canUndo != true)
            Button("Redo") { Task { await model?.redo() } }
                .disabled(model?.state?.canRedo != true)
            Button("Fit") { viewport.fit() }
                .keyboardShortcut("f", modifiers: [])
            Button(viewport.orthographic ? "Perspective" : "Orthographic") {
                viewport.orthographic.toggle()
            }
            .keyboardShortcut("o", modifiers: [])
            // Esc clears the selection (docs/INPUT.md).
            Button("Clear selection") { viewport.selection = .none }
                .keyboardShortcut(.escape, modifiers: [])
                .hidden()
                .frame(width: 0, height: 0)
        }
        .panel()
    }

    private var stats: some View {
        Text(
            String(
                format: "%.1f ms/frame (%.0f fps) · draw %.2f ms · %d triangles",
                viewport.frameMilliseconds,
                viewport.frameMilliseconds > 0 ? 1000 / viewport.frameMilliseconds : 0,
                viewport.drawMilliseconds, viewport.triangles)
        )
        .font(.caption.monospacedDigit())
        .panel()
    }

    private var selection: some View {
        Text(describe(viewport.selection)).font(.caption).panel()
    }

    private func describe(_ pick: Pick) -> String {
        switch pick.kind {
        case .none: "Nothing selected"
        case .face: "Face \(pick.id) selected"
        case .edge: "Edge \(pick.id) selected"
        case .vertex: "Vertex \(pick.id) selected"
        }
    }
}

/// Parameters and timeline of the open model.
private struct ModelPanel: View {
    let state: SessionState
    let model: DocumentModel

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            Text("Parameters").font(.caption.bold())
            ForEach(state.parameters) { parameter in
                ParameterRow(parameter: parameter) { value in
                    Task { await model.setParameter(parameter.name, to: value) }
                }
            }
            Text("Timeline").font(.caption.bold())
            ForEach(state.features) { feature in
                FeatureRow(feature: feature)
            }
            Text("Edits are not saved to the file yet (#11).")
                .font(.caption2)
                .foregroundStyle(.secondary)
        }
        .frame(width: 240, alignment: .leading)
        .panel()
    }
}

private struct ParameterRow: View {
    let parameter: SessionState.Parameter
    let onCommit: (Double) -> Void
    @State private var value = 0.0

    var body: some View {
        HStack {
            Text(parameter.name)
                .font(.caption)
                .frame(width: 60, alignment: .leading)
            TextField(parameter.name, value: $value, format: .number)
                .textFieldStyle(.roundedBorder)
                .font(.caption.monospacedDigit())
                #if os(iOS)
                    // Has a return key, unlike the decimal pad, so the value can be submitted.
                    .keyboardType(.numbersAndPunctuation)
                #endif
                .onSubmit {
                    if value != parameter.value { onCommit(value) }
                }
            Text("mm").font(.caption).foregroundStyle(.secondary)
        }
        .onAppear { value = parameter.value }
        .onChange(of: parameter.value) { _, newValue in value = newValue }
    }
}

private struct FeatureRow: View {
    let feature: SessionState.Feature

    var body: some View {
        VStack(alignment: .leading, spacing: 2) {
            Label(feature.name, systemImage: symbol)
                .font(.caption)
                .foregroundStyle(color)
            ForEach(feature.messages, id: \.self) { message in
                Text(message).font(.caption2).foregroundStyle(.secondary)
            }
        }
    }

    private var symbol: String {
        switch feature.status {
        case "ok": "checkmark.circle"
        case "warning": "exclamationmark.triangle"
        case "error": "xmark.octagon"
        case "suppressed": "pause.circle"
        case "rolledBack": "arrow.uturn.backward.circle"
        default: "circle.dotted"
        }
    }

    private var color: Color {
        switch feature.status {
        case "warning": .orange
        case "error": .red
        case "ok": .primary
        default: .secondary
        }
    }
}

extension View {
    fileprivate func panel() -> some View {
        padding(.horizontal, 12)
            .padding(.vertical, 8)
            .background(.regularMaterial, in: RoundedRectangle(cornerRadius: 10))
            .padding(12)
    }
}

#Preview {
    ContentView(document: .constant(FacetryDocument()))
}
