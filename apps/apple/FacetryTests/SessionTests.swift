import Foundation
import Testing

@testable import Facetry

/// The document part of the core API, with the M0.6 example document bundled in the app.
struct SessionTests {
    private func example() throws -> String {
        let url = try #require(Bundle.main.url(forResource: "corner-fillet", withExtension: "json"))
        return try String(contentsOf: url, encoding: .utf8)
    }

    /// 10 × 6 × 4 block with one vertical edge rounded by r = 1.5.
    private func cornerFilletVolume(width: Double) -> Double {
        width * 6 * 4 - (1 - .pi / 4) * 1.5 * 1.5 * 4
    }

    @Test func opensRegeneratesAndEdits() throws {
        let core = try Core()
        let session = try core.openSession(json: try example())
        #expect(try core.state(of: session).needsRegeneration)

        try core.regenerate(session)
        let state = try core.state(of: session)
        #expect(state.features.map(\.status) == ["ok", "ok", "ok"])
        #expect(state.bodies == ["f2"])
        #expect(state.parameters.first == .init(name: "width", value: 10))

        let shape = try core.bodyShape(session, body: "f2")
        let properties = try core.properties(of: shape)
        #expect(abs(properties.volume - cornerFilletVolume(width: 10)) < 1e-6)
        let names = try core.elementNames(session, body: "f2")
        #expect(names.faces.count == properties.faces)
        #expect(names.edges.count == properties.edges)
        try core.release(shape)

        try core.apply(session, [.setParameter(name: "width", value: 14)])
        try core.regenerate(session)
        let wider = try core.bodyShape(session, body: "f2")
        #expect(abs(try core.properties(of: wider).volume - cornerFilletVolume(width: 14)) < 1e-6)

        #expect(try core.undo(session))
        #expect(try core.state(of: session).canRedo)
        #expect(try core.redo(session))

        let reference = try core.reference(in: session, body: "f2", kind: .edge, id: 1)
        #expect(reference.contains("\"name\":\"edge{"))
        #expect(try core.save(session).contains("\"format\": \"facetry-document\""))
        try core.release(session)
    }

    @Test func progressCanCancel() throws {
        let core = try Core()
        let session = try core.openSession(json: try example())
        var calls = 0
        try core.regenerate(session) { done, total in
            calls += 1
            #expect(done == 1 && total == 3)
            return false
        }
        #expect(calls == 1)
        let state = try core.state(of: session)
        #expect(state.regeneration.cancelled)
        #expect(state.features.map(\.status) == ["ok", "notRun", "notRun"])
    }

    @Test func failingEditsThrowAndChangeNothing() throws {
        let core = try Core()
        let session = try core.openSession(json: try example())
        let before = try core.save(session)
        #expect(throws: CoreError.self) {
            try core.apply(session, [.setParameter(name: "missing", value: 1)])
        }
        #expect(throws: CoreError.self) { try core.apply(session, json: "{\"op\": \"explode\"}") }
        #expect(try core.save(session) == before)
        #expect(throws: CoreError.self) { try core.bodyShape(session, body: "f2") }
    }

    @Test func editsEncodeAsTheCoreExpects() throws {
        let encoded = try JSONEncoder().encode([
            DocumentEdit.setRollback(index: nil), .suppressFeature(id: "f5", suppressed: true),
        ])
        let json = try JSONSerialization.jsonObject(with: encoded) as? [[String: Any]]
        #expect(json?[0]["op"] as? String == "setRollback")
        #expect(json?[0]["index"] is NSNull)
        #expect(json?[1]["suppressed"] as? Bool == true)
    }
}
