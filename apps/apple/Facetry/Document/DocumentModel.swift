// The open model: a core session that runs off the main thread, and the state the UI shows.
// After every change the model hands the file's new bytes to the document, which SwiftUI saves.

import Foundation
import Observation
import Synchronization

/// What the UI needs after a regeneration.
struct ModelSnapshot: Sendable {
    let state: SessionState
    /// Display mesh of the first visible body; empty if none is visible. Showing several bodies
    /// at once comes with the first features that make more than one (M1.5).
    let mesh: MeshData
    /// A display mesh of each sketch's regions, by sketch ID: face ID i + 1 is region i.
    let regions: [String: MeshData]
}

/// Progress of a regeneration: `done` of `total` features are finished.
struct RegenerationProgress: Equatable, Sendable {
    let done: Int
    let total: Int
}

/// Stops regenerations that a newer change made obsolete (#13). Every change starts a new
/// generation; a regeneration of an older one stops after its current feature.
final class RegenerationControl: Sendable {
    private let latest = Atomic<Int>(0)

    /// Starts a new generation and returns it.
    func next() -> Int {
        latest.wrappingAdd(1, ordering: .relaxed).newValue
    }

    func isCurrent(_ generation: Int) -> Bool {
        latest.load(ordering: .relaxed) == generation
    }
}

/// Owns the core and one session. An actor, so that core calls never run on the main thread.
actor DocumentEngine {
    private let core: Core
    private let session: SessionHandle
    /// Used from the main thread while the engine is busy.
    nonisolated let control = RegenerationControl()

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

    /// Whether the document changed since the last complete regeneration.
    func needsRegeneration() throws -> Bool {
        try core.state(of: session).needsRegeneration
    }

    /// Regenerates and tessellates the first visible body. Returns nil once a newer generation
    /// started (see ``control``); the features that finished stay cached. A regeneration that
    /// takes longer than a moment reports its progress.
    func regenerate(generation: Int, progress: @escaping @Sendable (RegenerationProgress) -> Void)
        throws -> ModelSnapshot?
    {
        let control = control
        let start = ContinuousClock.now
        var reported = 0
        try core.regenerate(session) { done, total in
            guard control.isCurrent(generation) else { return false }
            if done > reported, ContinuousClock.now - start > .milliseconds(100) {
                reported = done
                progress(RegenerationProgress(done: done, total: total))
            }
            return true
        }
        guard control.isCurrent(generation) else { return nil }
        let state = try core.state(of: session)
        let mesh = { (shape: ShapeHandle) throws -> MeshData in
            defer { try? self.core.release(shape) }
            return try self.core.tessellate(shape, linearDeflection: 0.01, angularDeflection: 0.2)
        }
        var regions: [String: MeshData] = [:]
        for feature in state.features where !(feature.regions ?? []).isEmpty {
            regions[feature.id] = try mesh(core.sketchRegionsShape(session, sketch: feature.id))
        }
        guard let body = state.bodies.first(where: \.visible) else {
            return ModelSnapshot(state: state, mesh: .empty, regions: regions)
        }
        return ModelSnapshot(
            state: state, mesh: try mesh(core.bodyShape(session, body: body.id)), regions: regions)
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
    /// Progress of a regeneration that takes longer than a moment. The last model stays on
    /// screen meanwhile.
    private(set) var progress: RegenerationProgress?
    /// The generation whose model is on screen (see ``RegenerationControl``).
    private var shown = 0
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
            await regenerate(engine.control.next())
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

    // MARK: - Profiles

    /// The extrusion whose profiles are being chosen: the sketch's regions show over the model,
    /// taps add or remove them, and ``finishProfiles()`` takes them.
    var profiles: String? {
        didSet { showProfiles() }
    }
    /// Display meshes of the sketches' regions from the last regeneration.
    private var regionMeshes: [String: MeshData] = [:]

    /// The extrusion and the region names of its sketch, while choosing profiles.
    private var profileChoice: (extrude: SessionState.Feature, regions: [String])? {
        guard let profiles, let state,
            let extrude = state.features.first(where: { $0.id == profiles && $0.type == "extrude" }
            ),
            let sketch = extrude.definition["profile"]?.string
        else { return nil }
        return (extrude, state.features.first { $0.id == sketch }?.regions ?? [])
    }

    private func showProfiles() {
        guard let choice = profileChoice,
            let sketch = choice.extrude.definition["profile"]?.string,
            let mesh = regionMeshes[sketch]
        else {
            viewport.regions = nil
            return
        }
        let chosen: [String]
        if case .array(let names)? = choice.extrude.definition["regions"] {
            chosen = names.compactMap(\.string)
        } else {
            chosen = []
        }
        let ids = chosen.compactMap { name in
            choice.regions.firstIndex(of: name).map { UInt32($0 + 1) }
        }
        viewport.regions = RegionChoice(mesh: mesh, selected: ids)
    }

    /// Makes the extrusion take the regions selected in the viewport, as one undo step.
    func finishProfiles() async {
        guard let choice = profileChoice else {
            profiles = nil
            return
        }
        let names = viewport.selected.filter { $0.kind == .region }.compactMap { pick in
            let index = Int(pick.id) - 1
            return choice.regions.indices.contains(index) ? choice.regions[index] : nil
        }
        var definition = choice.extrude.definition
        definition["regions"] = names.isEmpty ? nil : .array(names.map { .string($0) })
        profiles = nil
        await edit([.replaceFeature(definition)])
    }

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
        // A regeneration still running for an earlier change is obsolete now. Stopping it lets
        // this change run sooner; dragging a value then shows only the models that keep up.
        let generation = engine.control.next()
        do {
            try await change(engine)
            error = nil
        } catch {
            self.error = "\(error)"
            // The change failed, but the regeneration it stopped still has to finish.
            if (try? await engine.needsRegeneration()) == true {
                await regenerate(generation)
            }
            return
        }
        await regenerate(generation)
        do {
            onSave(try await engine.saveFile())
        } catch {
            self.error = "Could not save: \(error)"
        }
    }

    private func regenerate(_ generation: Int) async {
        guard let engine else { return }
        pending += 1
        defer { pending -= 1 }
        do {
            let snapshot = try await engine.regenerate(generation: generation) { progress in
                Task { @MainActor in self.report(progress, of: generation) }
            }
            guard let snapshot else { return }  // a newer change took over
            shown = generation
            progress = nil
            state = snapshot.state
            ready = true
            // Element IDs change with the geometry, so an old selection would point elsewhere.
            viewport.selection = .none
            viewport.mesh = snapshot.mesh
            regionMeshes = snapshot.regions
            if profiles != nil { showProfiles() }
        } catch {
            shown = generation
            progress = nil
            self.error = "Regeneration failed: \(error)"
        }
    }

    private func report(_ progress: RegenerationProgress, of generation: Int) {
        // Reports can arrive after their regeneration has been shown.
        guard generation > shown else { return }
        self.progress = progress
    }
}
