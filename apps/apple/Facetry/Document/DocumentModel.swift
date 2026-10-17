// The open model: a core session that runs off the main thread, and the state the UI shows.
// After every change the model hands the file's new bytes to the document, which SwiftUI saves.

import Foundation
import Observation

/// What the UI needs after a regeneration.
struct ModelSnapshot: Sendable {
    let state: SessionState
    /// Display mesh of the first visible body; empty if none is visible. Showing several bodies
    /// at once comes with the first features that make more than one (M1.5).
    let mesh: MeshData
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

    /// A reference to an edge of a body, as JSON for a feature definition.
    func reference(body: String, edge: UInt32) throws -> JSONValue {
        let json = try core.reference(in: session, body: body, kind: .edge, id: edge)
        return try JSONDecoder().decode(JSONValue.self, from: Data(json.utf8))
    }

    /// The document as a .facetry file.
    func saveFile() throws -> Data {
        try core.saveFile(session)
    }

    /// Regenerates and tessellates the first visible body.
    func regenerate() throws -> ModelSnapshot {
        try core.regenerate(session)
        let state = try core.state(of: session)
        guard let body = state.bodies.first(where: \.visible) else {
            return ModelSnapshot(state: state, mesh: .empty)
        }
        let shape = try core.bodyShape(session, body: body.id)
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
        await edit([.setParameter(name: name, value: value)])
    }

    /// Applies edits as one undo step.
    func edit(_ edits: [DocumentEdit]) async {
        let steps = state?.undoCount ?? 0
        await run { try await $0.apply(edits) }
        registerUndo(after: steps)
    }

    // MARK: - Undo

    /// The window's undo manager. Edits register steps with it, so the Edit menu, Cmd+Z and the
    /// iPad's three-finger gestures undo and redo through the core.
    weak var undoManager: UndoManager?

    /// Registers the core's last step with the undo manager, if the core made one: a failed edit
    /// or one that changed nothing adds no step.
    private func registerUndo(after steps: Int) {
        guard let state, state.undoCount > steps, let name = state.undoName, let undoManager
        else { return }
        undoManager.registerUndo(withTarget: self) { model in
            MainActor.assumeIsolated { model.undoFromSystem(name) }
        }
        undoManager.setActionName(name)
    }

    /// Called by the undo manager: registers the redo step, then undoes in the core.
    private func undoFromSystem(_ name: String) {
        undoManager?.registerUndo(withTarget: self) { model in
            MainActor.assumeIsolated { model.redoFromSystem(name) }
        }
        undoManager?.setActionName(name)
        Task { await run { _ = try await $0.undo() } }
    }

    private func redoFromSystem(_ name: String) {
        undoManager?.registerUndo(withTarget: self) { model in
            MainActor.assumeIsolated { model.undoFromSystem(name) }
        }
        undoManager?.setActionName(name)
        Task { await run { _ = try await $0.redo() } }
    }

    /// A broken reference waiting for the user to pick its replacement in the viewport (ADR-0006).
    struct Repair: Equatable {
        let feature: String
        let index: Int
    }

    var repair: Repair?

    /// Replaces the broken reference ``repair`` with the picked edge.
    func completeRepair(with pick: Pick) async {
        guard let repair, pick.kind == .edge, let state,
            let body = state.bodies.first(where: \.visible)?.id,
            let feature = state.features.first(where: { $0.id == repair.feature }),
            case .array(let edges)? = feature.definition["edges"],
            edges.indices.contains(repair.index)
        else { return }
        self.repair = nil
        let steps = state.undoCount
        await run { engine in
            var replaced = edges
            replaced[repair.index] = try await engine.reference(body: body, edge: pick.id)
            var definition = feature.definition
            definition["edges"] = .array(replaced)
            try await engine.apply([.replaceFeature(definition)])
        }
        registerUndo(after: steps)
    }

    /// Undo and redo go through the undo manager when there is one, so it stays in step.
    func undo() async {
        if let undoManager, undoManager.canUndo {
            undoManager.undo()
        } else {
            await run { _ = try await $0.undo() }
        }
    }

    func redo() async {
        if let undoManager, undoManager.canRedo {
            undoManager.redo()
        } else {
            await run { _ = try await $0.redo() }
        }
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
            // Element IDs change with the geometry, so an old selection would point elsewhere.
            viewport.selection = .none
            viewport.mesh = snapshot.mesh
        } catch {
            self.error = "Regeneration failed: \(error)"
        }
    }
}
