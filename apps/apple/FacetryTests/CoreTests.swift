import Foundation
import Testing

@testable import Facetry

struct CoreTests {
    @Test func versionsMatchTheBuild() {
        let versions = CoreVersions.current
        #expect(versions.apiMajor == Core.supportedAPIMajor)
        #expect(versions.kernel == "8.0.1")
        #if os(macOS)
            #expect(versions.platform == "macos")
        #else
            #expect(versions.platform.hasPrefix("ios"))
        #endif
    }

    @Test func boxFilletTessellate() throws {
        let core = try Core()
        let box = try core.makeBox(20, 10, 5)
        let rounded = try core.fillet(box, edges: [1], radius: 1)

        let properties = try core.properties(of: rounded)
        #expect(properties.faces == 7)
        #expect(properties.volume < 1000)

        let mesh = try core.tessellate(rounded)
        #expect(mesh.triangleCount > 0)
        #expect(mesh.positions.count % 3 == 0)
        #expect(mesh.normals.count == mesh.positions.count)
        #expect(mesh.triangleFaceIDs.count == mesh.triangleCount)
        #expect(mesh.edgeOffsets.count == mesh.edgeIDs.count + 1)
        #expect(mesh.vertexIDs.count == properties.vertices)
        #expect((mesh.triangles.max() ?? 0) < UInt32(mesh.positions.count / 3))

        try core.release(rounded)
        try core.release(box)
    }

    @Test func cutHoleHasExpectedVolume() throws {
        let core = try Core()
        let plate = try core.boolean(
            .cut, try core.makeBox(20, 10, 5),
            try core.makeCylinder(x: 10, y: 5, radius: 2, height: 5))
        #expect(abs(try core.properties(of: plate).volume - (1000 - .pi * 20)) < 1e-6)
    }

    @Test func failuresThrowCoreErrorWithMessage() throws {
        let core = try Core()
        #expect {
            try core.makeBox(-1, 1, 1)
        } throws: { error in
            guard let error = error as? CoreError else { return false }
            return error.message.contains("positive")
        }

        let tiny = try core.makeBox(1, 1, 1)
        #expect(throws: CoreError.self) { try core.fillet(tiny, edges: [1], radius: 5) }
    }
}
