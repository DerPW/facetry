import SwiftUI

struct ContentView: View {
    @Binding var document: FacetryDocument
    @State private var viewport = ViewportModel()
    @State private var model: DocumentModel?
    @State private var selectedFeature: String?
    @Environment(\.undoManager) private var undoManager

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
                        BrowserPanel(state: state, model: model) { selectedFeature = $0 }
                            .panel()
                    }
                }
            }
            .overlay(alignment: .topTrailing) { controls }
            .overlay(alignment: .bottom) {
                VStack(spacing: 8) {
                    if model?.repair != nil {
                        Text("Select the edge that should replace the missing one.")
                            .font(.caption).panel()
                    }
                    if let model, let state = model.state {
                        TimelineBar(state: state, model: model, selected: $selectedFeature)
                    }
                }
                .padding(.bottom, 56)
            }
            .onChange(of: viewport.selection) { _, pick in
                Task { await model?.completeRepair(with: pick) }
            }
            .overlay(alignment: .bottomLeading) { stats }
            .overlay(alignment: .bottomTrailing) { selection }
            .task {
                let model = DocumentModel(viewport: viewport) { data in
                    // Marks the document as edited; SwiftUI saves it.
                    document.data = data
                }
                model.undoManager = undoManager
                self.model = model
                await model.load(document.data)
            }
    }

    private var header: some View {
        VStack(alignment: .leading, spacing: 2) {
            Text("Facetry").font(.headline)
            Text(
                "Version \(version) · tech preview\(model?.stress == true ? " · stress test" : "")")
            if let progress = model?.progress {
                Text("Regenerating… \(progress.done) of \(progress.total) features")
            }
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
                .help(model?.state?.undoName.map { "Undo \($0)" } ?? "Undo")
            Button("Redo") { Task { await model?.redo() } }
                .disabled(model?.state?.canRedo != true)
                .help(model?.state?.redoName.map { "Redo \($0)" } ?? "Redo")
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

/// The parameters of the open model.
private struct ModelPanel: View {
    let state: SessionState
    let model: DocumentModel

    /// Changing the units converts every length; the model keeps its size.
    private var units: Binding<LengthUnit> {
        Binding(
            get: { state.units },
            set: { units in Task { await model.edit([.setUnits(units)]) } })
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            HStack {
                Text("Parameters").font(.caption.bold())
                Spacer()
                Picker("Units", selection: units) {
                    ForEach(LengthUnit.allCases) { Text($0.symbol).tag($0) }
                }
                .pickerStyle(.menu)
                .font(.caption)
                .fixedSize()
            }
            ForEach(state.parameters) { parameter in
                ParameterRow(parameter: parameter, units: state.units) { value in
                    Task { await model.setParameter(parameter.name, to: value) }
                }
            }
        }
        .frame(width: 240, alignment: .leading)
        .panel()
    }
}

private struct ParameterRow: View {
    let parameter: SessionState.Parameter
    let units: LengthUnit
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
            Text(units.symbol).font(.caption).foregroundStyle(.secondary)
                .frame(width: 22, alignment: .leading)
        }
        .onAppear { value = parameter.value }
        .onChange(of: parameter.value) { _, newValue in value = newValue }
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
