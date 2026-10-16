import SwiftUI

struct ContentView: View {
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
        }
        .padding()
    }
}

#Preview {
    ContentView()
}
