import SwiftUI

struct ContentView: View {
    @State private var coreStatus = "Starting the core…"
    @State private var coreDetail = ""

    private var version: String {
        Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String ?? "?"
    }

    var body: some View {
        VStack(spacing: 12) {
            Image(systemName: "cube.transparent")
                .font(.system(size: 64, weight: .light))
                .foregroundStyle(.tint)
            Text("Facetry")
                .font(.largeTitle.weight(.semibold))
            Text("Version \(version) · Phase 0")
                .font(.subheadline)
                .foregroundStyle(.secondary)
            VStack(spacing: 4) {
                Text(coreStatus)
                Text(coreDetail).monospacedDigit()
            }
            .font(.footnote)
            .foregroundStyle(.secondary)
            .padding(.top, 12)
        }
        .padding()
        .task { await runCoreSmokeTest() }
    }

    /// Smoke test of the core, off the main thread: box → fillet → tessellate.
    private func runCoreSmokeTest() async {
        let result = await Task.detached(priority: .userInitiated) {
            () -> Result<(String, String), Error> in
            Result {
                let versions = CoreVersions.current
                let core = try Core()
                let box = try core.makeBox(20, 10, 5)
                let rounded = try core.fillet(box, edges: [1], radius: 2)
                let properties = try core.properties(of: rounded)
                let mesh = try core.tessellate(rounded)
                let status =
                    "Core \(versions.core) · OpenCASCADE \(versions.kernel) · \(versions.platform)"
                let detail =
                    "Filleted box: \(properties.faces) faces, \(mesh.triangleCount) triangles, "
                    + "\(mesh.edgeIDs.count) edges, \(String(format: "%.3f", properties.volume)) mm³"
                return (status, detail)
            }
        }.value
        switch result {
        case .success(let (status, detail)):
            coreStatus = status
            coreDetail = detail
        case .failure(let error):
            coreStatus = "The core failed to start: \(error)"
        }
    }
}

#Preview {
    ContentView()
}
