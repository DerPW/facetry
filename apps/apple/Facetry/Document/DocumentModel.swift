// The open model: a core session that runs off the main thread, and the state the UI shows.
// Until the native file format arrives (#11), every window opens the M0.6 example document and
// edits are not written to the file.

import Foundation
import Observation

/// What the UI needs after a regeneration.
struct ModelSnapshot: Sendable {
    let state: SessionState
    /// Display mesh of the first body (one body until the browser panel arrives, #15).
    let mesh: MeshData?
}

/// Owns the core and one session. An actor, so that core calls never run on the main thread.
actor DocumentEngine {
    private let core: Core
    private let session: SessionHandle
    private let fineMesh: Bool

    init(json: String?, fineMesh: Bool = false) throws {
        core = try Core()
        session = try core.openSession(json: json)
        self.fineMesh = fineMesh
    }

    func apply(_ edits: [DocumentEdit]) throws {
        try core.apply(session, edits)
    }

    func undo() throws -> Bool {
        try core.undo(session)
    }

    func redo() throws -> Bool {
        try core.redo(session)
    }

    func save() throws -> String {
        try core.save(session)
    }

    /// Regenerates and tessellates the first body.
    func regenerate() throws -> ModelSnapshot {
        try core.regenerate(session)
        let state = try core.state(of: session)
        guard let body = state.bodies.first else {
            return ModelSnapshot(state: state, mesh: nil)
        }
        let shape = try core.bodyShape(session, body: body)
        defer { try? core.release(shape) }
        let mesh = try core.tessellate(
            shape, linearDeflection: fineMesh ? 0.0004 : 0.01,
            angularDeflection: fineMesh ? 0.02 : 0.2)
        return ModelSnapshot(state: state, mesh: mesh)
    }
}

/// The model as the UI sees it. Edits queue up on the engine and run in order.
@MainActor
@Observable
final class DocumentModel {
    private(set) var state: SessionState?
    /// Number of regenerations running or waiting.
    private(set) var pending = 0
    var error: String?

    private let viewport: ViewportModel
    private var engine: DocumentEngine?

    init(viewport: ViewportModel) {
        self.viewport = viewport
    }

    /// Opens the example document and shows it.
    func load() async {
        do {
            let url = Bundle.main.url(forResource: "corner-fillet", withExtension: "json")
            guard let url else { throw CocoaError(.fileNoSuchFile) }
            let json = try String(contentsOf: url, encoding: .utf8)
            // `--stress` tessellates finely for performance checks.
            let fine = ProcessInfo.processInfo.arguments.contains("--stress")
            engine = try DocumentEngine(json: json, fineMesh: fine)
            await regenerate()
        } catch {
            self.error = "Could not open the document: \(error)"
        }
    }

    func setParameter(_ name: String, to value: Double) async {
        await run { try await $0.apply([.setParameter(name: name, value: value)]) }
    }

    func undo() async {
        await run { _ = try await $0.undo() }
    }

    func redo() async {
        await run { _ = try await $0.redo() }
    }

    private func run(_ change: (DocumentEngine) async throws -> Void) async {
        guard let engine else { return }
        do {
            try await change(engine)
            error = nil
        } catch {
            self.error = "\(error)"
            return
        }
        await regenerate()
    }

    private func regenerate() async {
        guard let engine else { return }
        pending += 1
        defer { pending -= 1 }
        do {
            let snapshot = try await engine.regenerate()
            state = snapshot.state
            if let mesh = snapshot.mesh {
                // Element IDs change with the geometry, so an old selection would point elsewhere.
                viewport.selection = .none
                viewport.mesh = mesh
            }
        } catch {
            self.error = "Regeneration failed: \(error)"
        }
    }
}
