// The open model: a core session that runs off the main thread, and the state the UI shows.
// After every change the model hands the file's new bytes to the document, which SwiftUI saves.

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

    init(fileData: Data) throws {
        core = try Core()
        session = try core.openSession(fileData: fileData)
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

    /// The document as a .facetry file.
    func saveFile() throws -> Data {
        try core.saveFile(session)
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
        let mesh = try core.tessellate(shape, linearDeflection: 0.01, angularDeflection: 0.2)
        return ModelSnapshot(state: state, mesh: mesh)
    }

    /// The M0.4 demo part (a plate with rounded edges, a boss and a hole), tessellated finely to
    /// about 215k triangles. Used by `--stress`, so the numbers in docs/benchmarks.md stay
    /// comparable.
    func stressMesh() throws -> MeshData {
        let plate = try core.makeBox(40, 30, 4)
        let rounded = try core.fillet(plate, edges: Array(1...12), radius: 1.5)
        let boss = try core.makeCylinder(x: 20, y: 15, radius: 6, height: 12)
        let joined = try core.boolean(.fuse, rounded, boss)
        let hole = try core.makeCylinder(x: 20, y: 15, radius: 3, height: 12)
        let part = try core.boolean(.cut, joined, hole)
        return try core.tessellate(part, linearDeflection: 0.0004, angularDeflection: 0.02)
    }
}

/// The model as the UI sees it. Edits queue up on the engine and run in order.
@MainActor
@Observable
final class DocumentModel {
    private(set) var state: SessionState?
    /// Number of regenerations running or waiting.
    private(set) var pending = 0
    /// The core is running and the viewport shows something.
    private(set) var ready = false
    var error: String?
    /// `--stress` shows the finely tessellated M0.4 demo part instead of the document.
    let stress = ProcessInfo.processInfo.arguments.contains("--stress")

    private let viewport: ViewportModel
    private var engine: DocumentEngine?
    /// Receives the file's bytes after every change.
    private let onSave: (Data) -> Void

    init(viewport: ViewportModel, onSave: @escaping (Data) -> Void) {
        self.viewport = viewport
        self.onSave = onSave
    }

    /// Opens a .facetry file and shows it (or the stress part, see ``stress``).
    func load(_ fileData: Data) async {
        do {
            let engine = try DocumentEngine(fileData: fileData)
            self.engine = engine
            if stress {
                viewport.mesh = try await engine.stressMesh()
                ready = true
                return
            }
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
        do {
            onSave(try await engine.saveFile())
        } catch {
            self.error = "Could not save: \(error)"
        }
    }

    private func regenerate() async {
        guard let engine else { return }
        pending += 1
        defer { pending -= 1 }
        do {
            let snapshot = try await engine.regenerate()
            state = snapshot.state
            ready = true
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
