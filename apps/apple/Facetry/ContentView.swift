import SwiftUI

struct ContentView: View {
    @State private var viewport = ViewportModel()
    @State private var coreStatus = "Starting the core…"

    private var version: String {
        Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String ?? "?"
    }

    var body: some View {
        ViewportView(model: viewport)
            .ignoresSafeArea()
            .overlay(alignment: .topLeading) { header }
            .overlay(alignment: .topTrailing) { controls }
            .overlay(alignment: .bottomLeading) { stats }
            .overlay(alignment: .bottomTrailing) { selection }
            .task { await loadDemoPart() }
    }

    private var header: some View {
        VStack(alignment: .leading, spacing: 2) {
            Text("Facetry").font(.headline)
            Text("Version \(version) · Phase 0")
            Text(viewport.error ?? coreStatus)
        }
        .font(.caption)
        .foregroundStyle(.secondary)
        .panel()
    }

    private var controls: some View {
        HStack {
            Button("Fit") { viewport.fit() }
            Button(viewport.orthographic ? "Perspective" : "Orthographic") {
                viewport.orthographic.toggle()
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

    /// Builds the demo part off the main thread: a plate with rounded edges, a boss on top and a
    /// hole through both.
    private func loadDemoPart() async {
        let result = await Task.detached(priority: .userInitiated) {
            () -> Result<(String, MeshData), Error> in
            Result {
                let versions = CoreVersions.current
                let core = try Core()
                let plate = try core.makeBox(40, 30, 4)
                let rounded = try core.fillet(plate, edges: Array(1...12), radius: 1.5)
                let boss = try core.makeCylinder(x: 20, y: 15, radius: 6, height: 12)
                let joined = try core.boolean(.fuse, rounded, boss)
                let hole = try core.makeCylinder(x: 20, y: 15, radius: 3, height: 12)
                let part = try core.boolean(.cut, joined, hole)
                // `--stress` tessellates finely (about 200k triangles) for performance checks.
                let stress = ProcessInfo.processInfo.arguments.contains("--stress")
                let mesh = try core.tessellate(
                    part, linearDeflection: stress ? 0.0004 : 0.01,
                    angularDeflection: stress ? 0.02 : 0.2)
                let status =
                    "Core \(versions.core) · OpenCASCADE \(versions.kernel) · \(versions.platform)"
                return (status, mesh)
            }
        }.value
        switch result {
        case .success(let (status, mesh)):
            coreStatus = status
            viewport.mesh = mesh
        case .failure(let error):
            coreStatus = "The core failed to start: \(error)"
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
    ContentView()
}
