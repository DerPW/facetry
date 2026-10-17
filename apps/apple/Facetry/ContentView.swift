import SwiftUI

struct ContentView: View {
    @Binding var document: FacetryDocument
    @State private var viewport = ViewportModel()
    @State private var model: DocumentModel?
    @State private var selectedFeature: String?
    @Environment(\.undoManager) private var undoManager
    // Navigation settings belong to the device, not the document (docs/INPUT.md).
    @AppStorage("navigationPreset") private var navigationPreset = NavigationPreset.facetry
    @AppStorage("oneFingerPans") private var oneFingerPans = false
    @AppStorage("grid") private var grid = true
    @AppStorage("visualStyle") private var style = VisualStyle.shadedEdges

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
                        BrowserPanel(
                            state: state, model: model,
                            selectFeature: { selectedFeature = $0 },
                            selectBody: { viewport.selectBody() }
                        )
                        .panel()
                    }
                }
            }
            .overlay(alignment: .topTrailing) {
                VStack(alignment: .trailing, spacing: 0) {
                    controls
                    ViewCubeView(model: viewport)
                }
            }
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
            .onChange(of: navigationPreset, initial: true) { _, preset in
                viewport.navigation = preset
            }
            .onChange(of: oneFingerPans, initial: true) { _, pans in
                viewport.oneFingerPans = pans
            }
            .onChange(of: grid, initial: true) { _, shown in viewport.grid = shown }
            .onChange(of: style, initial: true) { _, style in viewport.style = style }
            // The shown body's opacity comes from the document.
            .onChange(of: model?.state?.bodies.first(where: \.visible)?.opacity, initial: true) {
                _, opacity in viewport.opacity = opacity ?? 1
            }
            // The grid's spacing follows the document's units.
            .onChange(of: model?.state?.units, initial: true) { _, units in
                viewport.gridUnitMillimeters = (units ?? .millimeter).millimeters
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
            // How the view looks; kept in one menu so the controls fit next to the panels.
            Menu("Display") {
                Toggle("Orthographic", isOn: $viewport.orthographic)
                Toggle("Grid", isOn: $grid)
                Picker("Style", selection: $style) {
                    ForEach(VisualStyle.allCases) { Text($0.title).tag($0) }
                }
                Picker("Navigation", selection: $navigationPreset) {
                    ForEach(NavigationPreset.allCases) { Text($0.title).tag($0) }
                }
                #if os(iOS)
                    Toggle("One finger pans", isOn: $oneFingerPans)
                #endif
            }
            .fixedSize()
            Picker("Select", selection: $viewport.pickFilter) {
                ForEach(PickFilter.allCases) { Text($0.title).tag($0) }
            }
            .pickerStyle(.menu)
            .fixedSize()
            Toggle("Multi", isOn: $viewport.multiSelect)
                .toggleStyle(.button)
            // Shortcuts of the menu items (docs/INPUT.md): O projection, G grid.
            Button("Toggle projection") { viewport.orthographic.toggle() }
                .keyboardShortcut("o", modifiers: [])
                .hidden()
                .frame(width: 0, height: 0)
            Button("Toggle grid") { grid.toggle() }
                .keyboardShortcut("g", modifiers: [])
                .hidden()
                .frame(width: 0, height: 0)
            // Esc clears the selection (docs/INPUT.md).
            Button("Clear selection") { viewport.selection = .none }
                .keyboardShortcut(.escape, modifiers: [])
                .hidden()
                .frame(width: 0, height: 0)
            // Keys 1–5 choose what clicks select.
            ForEach(PickFilter.allCases) { filter in
                Button(filter.title) { viewport.pickFilter = filter }
                    .keyboardShortcut(filter.key, modifiers: [])
                    .hidden()
                    .frame(width: 0, height: 0)
            }
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
        Text(describe(viewport.selected)).font(.caption).panel()
    }

    private func describe(_ selection: [Pick]) -> String {
        let names: [(Pick.Kind, String, String)] = [
            (.vertex, "Vertex", "vertices"), (.edge, "Edge", "edges"), (.face, "Face", "faces"),
            (.body, "Body", "bodies"),
        ]
        guard let only = selection.first else { return "Nothing selected" }
        if selection.count == 1 {
            if only.kind == .body { return "Body selected" }
            let name = names.first { $0.0 == only.kind }?.1 ?? "Element"
            return "\(name) \(only.id) selected"
        }
        let parts = names.compactMap { kind, singular, plural -> String? in
            let count = selection.filter { $0.kind == kind }.count
            guard count > 0 else { return nil }
            return "\(count) \(count == 1 ? singular.lowercased() : plural)"
        }
        return parts.joined(separator: ", ") + " selected"
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
