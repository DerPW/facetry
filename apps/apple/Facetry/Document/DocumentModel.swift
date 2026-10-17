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
    /// A display mesh of each sketch's curves and points, by sketch ID.
    let sketches: [String: MeshData]
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

    /// A reference to an edge or face of a body, as JSON for a feature definition.
    func reference(body: String, kind: ElementKind = .edge, id: UInt32) throws -> JSONValue {
        let json = try core.reference(in: session, body: body, kind: kind, id: id)
        return try JSONDecoder().decode(JSONValue.self, from: Data(json.utf8))
    }

    /// Groups the following edits into one undo step (a tool session, such as a sketch).
    func beginTransaction(_ name: String) throws {
        try core.beginTransaction(session, name: name)
    }

    /// Keeps the transaction's edits as one undo step, or restores the document from before.
    func endTransaction(commit: Bool) throws {
        try core.endTransaction(session, commit: commit)
    }

    // Sketch tools (docs/adr/0015-sketch-tools.md).

    /// A tool; move and copy take `selection`.
    func makeTool(
        sketch: String, kind: SketchToolKind, construction: Bool, selection: [String]
    ) throws -> ToolHandle {
        let tool = try core.createSketchTool(sketch: sketch, kind: kind, construction: construction)
        if kind == .move || kind == .copy {
            try core.setSketchToolSelection(tool, entities: selection)
        }
        return tool
    }

    /// A pointer event for the tool, with the grid spacing to snap to (0: none): whether the
    /// document changed, the tool's preview and its snap feedback.
    func toolEvent(
        _ tool: ToolHandle, _ event: ToolEvent, u: Double, v: Double, snapRadius: Double,
        grid: Double
    ) throws -> (changed: Bool, preview: MeshData, feedback: SnapFeedback) {
        try core.setSketchToolGrid(tool, spacing: grid)
        let changed = try core.sketchToolPointer(
            session, tool: tool, event: event, u: u, v: v, snapRadius: snapRadius)
        return (
            changed, try core.sketchToolPreview(session, tool: tool),
            try core.sketchToolFeedback(tool)
        )
    }

    func cancelTool(_ tool: ToolHandle) throws -> Bool {
        try core.cancelSketchTool(tool)
    }

    func releaseTool(_ tool: ToolHandle) throws {
        try core.releaseSketchTool(tool)
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
        var sketches: [String: MeshData] = [:]
        for feature in state.features {
            if !(feature.regions ?? []).isEmpty {
                regions[feature.id] = try mesh(core.sketchRegionsShape(session, sketch: feature.id))
            }
            if feature.frame != nil {
                sketches[feature.id] = try core.sketchMesh(session, sketch: feature.id)
            }
        }
        let body = try state.bodies.first(where: \.visible).map {
            try mesh(core.bodyShape(session, body: $0.id))
        }
        return ModelSnapshot(
            state: state, mesh: body ?? .empty, regions: regions, sketches: sketches)
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

    // MARK: - Sketch mode

    /// The sketch being edited. Everything done meanwhile is one undo step (a transaction), so
    /// cancelling restores the document from before.
    private(set) var sketching: String?
    /// Display meshes of the sketches from the last regeneration.
    private var sketchMeshes: [String: MeshData] = [:]
    /// The undo steps before sketch mode started, to register the sketch's step afterwards.
    private var stepsBeforeSketch = 0

    /// Starts a new sketch on an origin plane ("xy", "xz", "yz") or on the selected face
    /// ("face"), at the rollback marker.
    func newSketch(on plane: String) async {
        guard sketching == nil, let state else { return }
        let face = viewport.selected.first { $0.kind == .face }
        let body = state.bodies.first(where: \.visible)?.id
        if plane == "face", face == nil || body == nil { return }
        let before = Set(state.features.map(\.id))
        stepsBeforeSketch = state.undoCount
        await run { engine in
            var feature: [String: JSONValue] = [
                "type": .string("sketch"), "plane": .string(plane), "points": .array([]),
                "lines": .array([]),
            ]
            if plane == "face", let face, let body {
                feature["face"] = try await engine.reference(body: body, kind: .face, id: face.id)
            }
            try await engine.beginTransaction("Sketch")
            do {
                try await engine.apply([.addFeature(.object(feature), index: state.rollback)])
            } catch {
                try? await engine.endTransaction(commit: false)
                throw error
            }
        }
        sketching = self.state?.features.first { !before.contains($0.id) }?.id
        showSketch(turning: true)
    }

    /// Opens a sketch for editing.
    func editSketch(_ id: String) async {
        guard sketching == nil, let state else { return }
        stepsBeforeSketch = state.undoCount
        await run { try await $0.beginTransaction("Edit sketch") }
        sketching = id
        showSketch(turning: true)
    }

    /// Ends sketch mode, keeping what was done as one undo step or restoring the document.
    func finishSketch(keep: Bool) async {
        guard sketching != nil else { return }
        tool = nil
        sketching = nil
        viewport.sketch = nil
        viewport.drawing = nil
        viewport.glyphs = []
        viewport.pointerLabel = ""
        await run { try await $0.endTransaction(commit: keep) }
        if keep { registerUndo(after: stepsBeforeSketch) }
    }

    /// Names for what the pointer snapped to and what the tool infers.
    private static let snapNames = [
        "point": "Point", "center": "Center", "midpoint": "Midpoint", "quadrant": "Quadrant",
        "intersection": "Intersection", "origin": "Origin", "onCurve": "On curve", "grid": "Grid",
        "horizontal": "Horizontal", "vertical": "Vertical", "parallel": "Parallel",
        "perpendicular": "Perpendicular", "tangent": "Tangent",
    ]

    private func describe(_ feedback: SnapFeedback) -> String {
        var labels = ([feedback.snap] + feedback.inferences).compactMap { Self.snapNames[$0] }
        if feedback.radius > 0 {
            let units = state?.units ?? .millimeter
            let value = (feedback.radius / units.millimeters * 1000).rounded() / 1000
            labels.append("R \(value.formatted()) \(units.symbol)")
        }
        return labels.joined(separator: " · ")
    }

    /// Constraint glyphs on the sketch (docs/adr/0015-sketch-tools.md).
    private static let glyphTexts = [
        "horizontal": "H", "vertical": "V", "parallel": "∥", "perpendicular": "⊥", "tangent": "T",
        "equal": "=", "coincident": "⊙", "midpoint": "△", "concentric": "◎", "fix": "F",
    ]

    /// Draws the sketch being edited; `turning` turns the view to face it.
    private func showSketch(turning: Bool) {
        guard let sketching, let feature = state?.features.first(where: { $0.id == sketching }),
            let mesh = sketchMeshes[sketching]
        else {
            viewport.sketch = nil
            viewport.glyphs = []
            return
        }
        viewport.glyphs = (feature.glyphs ?? []).compactMap { glyph in
            guard glyph.at.count == 3 else { return nil }
            return GlyphLabel(
                text: Self.glyphTexts[glyph.type] ?? "?",
                at: SIMD3(Float(glyph.at[0]), Float(glyph.at[1]), Float(glyph.at[2])))
        }
        let construction = feature.sketchCurves.enumerated().compactMap { index, curve in
            curve.construction ? UInt32(index + 1) : nil
        }
        viewport.sketch = SketchDisplay(mesh: mesh, construction: construction)
        updateDrawing()
        if turning, let normal = feature.frame?.normal, normal.count == 3 {
            viewport.view(from: SIMD3(Float(normal[0]), Float(normal[1]), Float(normal[2])))
        }
    }

    // MARK: - Drawing

    /// The drawing tool in sketch mode (nil: selecting), and whether it draws construction
    /// curves.
    var tool: SketchToolKind? {
        didSet { if tool != oldValue { configureTool() } }
    }
    var construction = false {
        didSet { if construction != oldValue { configureTool() } }
    }
    /// Free points snap to the grid.
    var gridSnap = false

    /// Everything a tool does goes through one queue, so its creation, pointer events and
    /// release reach the engine in order. Moves that pile up collapse into the latest.
    private enum ToolCommand {
        case configure(SketchToolKind?, construction: Bool, selection: [String])
        case pointer(ToolEvent, u: Double, v: Double, snapRadius: Double, grid: Double)
        case cancel
    }
    private var toolCommands: [ToolCommand] = []
    private var toolRunning = false
    private var toolHandle: ToolHandle?

    private func configureTool() {
        queueTool(
            .configure(
                sketching != nil ? tool : nil, construction: construction,
                selection: selectedSketchEntities))
        updateDrawing()
    }

    /// Routes the pointer to the tool while one is chosen.
    private func updateDrawing() {
        guard sketching != nil, tool != nil,
            let frame = state?.features.first(where: { $0.id == sketching })?.frame,
            frame.origin.count == 3, frame.xAxis.count == 3, frame.yAxis.count == 3
        else {
            viewport.drawing = nil
            return
        }
        let vector = { (v: [Double]) in SIMD3(Float(v[0]), Float(v[1]), Float(v[2])) }
        let plane = SketchPlane(
            origin: vector(frame.origin), xAxis: vector(frame.xAxis), yAxis: vector(frame.yAxis))
        viewport.drawing = DrawingInput(plane: plane) { [weak self] event, u, v, snapRadius, grid in
            guard let self else { return }
            queueTool(
                .pointer(event, u: u, v: v, snapRadius: snapRadius, grid: gridSnap ? grid : 0))
        }
    }

    /// Esc: drops the shape in progress, or leaves the tool when nothing is in progress.
    func cancelTool() {
        queueTool(.cancel)
    }

    /// The construction toggle: changes the selected sketch curves, or else what the tool draws
    /// next.
    func toggleConstruction() async {
        guard let sketching, let feature = state?.features.first(where: { $0.id == sketching })
        else { return }
        let curves = feature.sketchCurves
        let selected = viewport.selected.filter { $0.kind == .sketchCurve }.compactMap {
            pick -> (id: String, construction: Bool)? in
            let index = Int(pick.id) - 1
            return curves.indices.contains(index) ? curves[index] : nil
        }
        guard !selected.isEmpty else {
            construction.toggle()
            return
        }
        await edit([
            .setConstruction(
                sketch: sketching, entities: selected.map(\.id),
                construction: !selected.allSatisfy(\.construction))
        ])
    }

    /// The selected points and curves of the sketch being edited.
    var selectedSketchEntities: [String] {
        guard let sketching, let feature = state?.features.first(where: { $0.id == sketching })
        else { return [] }
        let curves = feature.sketchCurves
        let points = feature.sketchPoints
        return viewport.selected.compactMap { pick in
            let index = Int(pick.id) - 1
            switch pick.kind {
            case .sketchCurve: return curves.indices.contains(index) ? curves[index].id : nil
            case .sketchPoint: return points.indices.contains(index) ? points[index] : nil
            default: return nil
            }
        }
    }

    /// Delete: removes the selected points and curves, and what depends on them.
    func deleteSelected() async {
        let entities = selectedSketchEntities
        guard let sketching, tool == nil, !entities.isEmpty else { return }
        viewport.selection = .none
        await edit([.deleteSketchEntities(sketch: sketching, entities: entities)])
    }

    private func queueTool(_ command: ToolCommand) {
        if case .pointer(.move, _, _, _, _) = command,
            case .pointer(.move, _, _, _, _)? = toolCommands.last
        {
            toolCommands[toolCommands.count - 1] = command
        } else {
            toolCommands.append(command)
        }
        guard !toolRunning else { return }
        toolRunning = true
        Task { await runToolCommands() }
    }

    private func runToolCommands() async {
        defer { toolRunning = false }
        while !toolCommands.isEmpty {
            let command = toolCommands.removeFirst()
            guard let engine else { continue }
            do {
                switch command {
                case .configure(let kind, let construction, let selection):
                    if let old = toolHandle {
                        toolHandle = nil
                        try? await engine.releaseTool(old)
                    }
                    viewport.sketchPreview = nil
                    viewport.pointerLabel = ""
                    if let kind, let sketching {
                        toolHandle = try await engine.makeTool(
                            sketch: sketching, kind: kind, construction: construction,
                            selection: selection)
                    }
                case .pointer(let event, let u, let v, let snapRadius, let grid):
                    guard let toolHandle else { continue }
                    let result = try await engine.toolEvent(
                        toolHandle, event, u: u, v: v, snapRadius: snapRadius, grid: grid)
                    viewport.sketchPreview = result.preview
                    viewport.pointerLabel = describe(result.feedback)
                    if result.changed { await run { _ in } }
                case .cancel:
                    viewport.sketchPreview = nil
                    if let toolHandle, try await engine.cancelTool(toolHandle) { continue }
                    tool = nil
                }
            } catch {
                self.error = "\(error)"
            }
        }
    }

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
            replaced[repair.index] = try await engine.reference(body: body, id: pick.id)
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
            sketchMeshes = snapshot.sketches
            if sketching != nil { showSketch(turning: false) }
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
