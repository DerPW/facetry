import Foundation
import Testing

@testable import Facetry

/// The document part of the core API, with the M0.6 example document bundled in the app.
struct SessionTests {
    private func openExample(_ core: Core) throws -> SessionHandle {
        try core.openSession(fileData: FacetryDocument.example)
    }

    /// 10 × 6 × 4 block with one vertical edge rounded by r = 1.5.
    private func cornerFilletVolume(width: Double) -> Double {
        width * 6 * 4 - (1 - .pi / 4) * 1.5 * 1.5 * 4
    }

    @Test func opensRegeneratesAndEdits() throws {
        let core = try Core()
        let session = try openExample(core)
        #expect(try core.state(of: session).needsRegeneration)

        try core.regenerate(session)
        let state = try core.state(of: session)
        #expect(state.features.map(\.status) == ["ok", "ok", "ok"])
        #expect(state.bodies == [.init(id: "f2", name: "Body 1", visible: true, opacity: 1)])
        #expect(state.origin.map(\.id) == ["xy", "xz", "yz", "x", "y", "z", "o"])
        #expect(state.parameters.first == .init(name: "width", value: 10))

        let shape = try core.bodyShape(session, body: "f2")
        let properties = try core.properties(of: shape)
        #expect(abs(properties.volume - cornerFilletVolume(width: 10)) < 1e-6)
        let names = try core.elementNames(session, body: "f2")
        #expect(names.faces.count == properties.faces)
        #expect(names.edges.count == properties.edges)
        try core.release(shape)

        // The sketch's one region (profile), with faces to show and pick.
        #expect(state.features[0].regions == ["region{f1/l1+|f1/l2+|f1/l3+|f1/l4+}"])
        #expect(state.features[1].regions == nil)
        let regions = try core.sketchRegionsShape(session, sketch: "f1")
        #expect(try core.properties(of: regions).faces == 1)
        try core.release(regions)

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

    @Test func definitionsGoBackUnchangedOrEdited() throws {
        let core = try Core()
        let session = try openExample(core)
        try core.regenerate(session)
        let features = try core.state(of: session).features
        #expect(features[0].dependents == ["f2", "f5"])
        // The fillet's definition carries references with shadows (integer IDs, coordinates).
        try core.apply(session, [.replaceFeature(features[2].definition)])
        var extrude = features[1].definition
        extrude["distance"] = .number(7)
        try core.apply(session, [.replaceFeature(extrude)])
        #expect(try core.state(of: session).features[1].definition["distance"] == .number(7))
    }

    @Test func progressCanCancel() throws {
        let core = try Core()
        let session = try openExample(core)
        // Stops once the sketch is done; it reports progress (done = 0) while it finds its
        // profiles.
        var calls: [Int] = []
        try core.regenerate(session) { done, total in
            calls.append(done)
            #expect(total == 3)
            return done < 1
        }
        #expect(calls.last == 1)
        let state = try core.state(of: session)
        #expect(state.regeneration.cancelled)
        #expect(state.features.map(\.status) == ["ok", "notRun", "notRun"])
    }

    @Test func failingEditsThrowAndChangeNothing() throws {
        let core = try Core()
        let session = try openExample(core)
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

    @Test func sketchesShowTheirCurvesAndOpenInTransactions() throws {
        let core = try Core()
        let session = try openExample(core)
        try core.regenerate(session)
        let sketch = try core.state(of: session).features[0]
        #expect(sketch.frame?.normal == [0, 0, 1])
        #expect(sketch.sketchCurves.map(\.id) == ["l1", "l2", "l3", "l4"])
        let mesh = try core.sketchMesh(session, sketch: "f1")
        #expect(mesh.edgeIDs == [1, 2, 3, 4])
        #expect(mesh.vertexIDs.count == 4)
        #expect(mesh.triangles.isEmpty)

        // A new sketch on XZ in a transaction: one undo step, or nothing when cancelled.
        let steps = try core.state(of: session).undoCount
        let feature: JSONValue = .object([
            "type": .string("sketch"), "plane": .string("xz"), "points": .array([]),
            "lines": .array([]),
        ])
        try core.beginTransaction(session, name: "Sketch")
        try core.apply(session, [.addFeature(feature, index: nil)])
        try core.endTransaction(session, commit: false)
        #expect(try core.state(of: session).features.count == 3)
        try core.beginTransaction(session, name: "Sketch")
        try core.apply(session, [.addFeature(feature, index: nil)])
        try core.endTransaction(session, commit: true)
        try core.regenerate(session)
        let state = try core.state(of: session)
        #expect(state.features.count == 4)
        #expect(state.undoCount == steps + 1)
        #expect(state.features[3].frame?.normal == [0, -1, 0])
    }

    @Test func sketchToolsDrawInTheCore() throws {
        let core = try Core()
        let session = try openExample(core)
        try core.regenerate(session)
        try core.beginTransaction(session, name: "Sketch")
        let tool = try core.createSketchTool(sketch: "f1", kind: .circleCenter, construction: true)
        let click = { (u: Double, v: Double) throws -> Bool in
            _ = try core.sketchToolPointer(
                session, tool: tool, event: .down, u: u, v: v, snapRadius: 0.1)
            return try core.sketchToolPointer(
                session, tool: tool, event: .up, u: u, v: v, snapRadius: 0.1)
        }
        #expect(try click(5, 3) == false)
        _ = try core.sketchToolPointer(
            session, tool: tool, event: .move, u: 6, v: 3, snapRadius: 0.1)
        #expect(try core.sketchToolPreview(session, tool: tool).edgeIDs == [1])
        #expect(try click(6, 3))
        #expect(try core.cancelSketchTool(tool) == false)
        _ = try core.sketchToolPointer(
            session, tool: tool, event: .move, u: 5.02, v: 3, snapRadius: 0.1)
        #expect(try core.sketchToolFeedback(tool).snap == "center")
        try core.setSketchToolGrid(tool, spacing: 1)
        try core.releaseSketchTool(tool)
        try core.endTransaction(session, commit: true)
        try core.regenerate(session)
        let sketch = try core.state(of: session).features[0]
        #expect(sketch.sketchCurves.last?.construction == true)
        #expect(sketch.glyphs?.isEmpty == true)  // the example's sketch has no constraints
        #expect(sketch.sketchCurves.count == 5)

        // The construction flag goes back off with an edit.
        let circle = try #require(sketch.sketchCurves.last?.id)
        try core.apply(
            session, [.setConstruction(sketch: "f1", entities: [circle], construction: false)])
        #expect(try core.state(of: session).features[0].sketchCurves.last?.construction == false)
    }

    @Test func sketchEditingToolsAndDelete() throws {
        let core = try Core()
        let session = try openExample(core)
        try core.regenerate(session)
        let tool = try core.createSketchTool(sketch: "f1", kind: .copy, construction: false)
        try core.setSketchToolSelection(tool, entities: ["l1", "l2", "l3", "l4"])
        let click = { (u: Double, v: Double) throws -> Bool in
            _ = try core.sketchToolPointer(
                session, tool: tool, event: .down, u: u, v: v, snapRadius: 0.1)
            return try core.sketchToolPointer(
                session, tool: tool, event: .up, u: u, v: v, snapRadius: 0.1)
        }
        #expect(try click(0, 0) == false)
        #expect(try click(20, 0))
        #expect(try core.sketchToolFeedback(tool).radius == 0)
        try core.releaseSketchTool(tool)
        let sketch = try core.state(of: session).features[0]
        #expect(sketch.sketchCurves.count == 8)
        #expect(Array(sketch.sketchPoints.prefix(4)) == ["p1", "p2", "p3", "p4"])

        // Deleting a corner of the copy takes its two sides.
        try core.apply(
            session, [.deleteSketchEntities(sketch: "f1", entities: [sketch.sketchPoints[4]])])
        #expect(try core.state(of: session).features[0].sketchCurves.count == 6)
    }

    @Test func originItemsAndFacesProjectIntoSketches() throws {
        let core = try Core()
        let session = try openExample(core)
        try core.regenerate(session)
        let feature: JSONValue = .object([
            "type": .string("sketch"), "plane": .string("xy"), "points": .array([]),
            "lines": .array([]),
        ])
        try core.apply(session, [.addFeature(feature, index: nil)])
        try core.regenerate(session)
        let sketch = try #require(try core.state(of: session).features.last?.id)
        #expect(try core.projectIntoSketch(session, sketch: sketch, sources: [.origin("x")]) == 1)
        // The block's bottom face projects onto XY as its outline: four lines.
        let added = try core.projectIntoSketch(
            session, sketch: sketch, sources: [.face(body: "f2", id: 1), .face(body: "f2", id: 2)])
        #expect(added > 0)
        try core.regenerate(session)
        let projected = try #require(try core.state(of: session).features.last)
        #expect(projected.status == "ok")
        #expect(projected.sketchProjectedCurves.count == 1 + added)
        #expect(throws: CoreError.self) {
            try core.projectIntoSketch(session, sketch: sketch, sources: [.origin("z")])
        }
    }

    @Test func unitsChangeAndConvertEveryLength() throws {
        let core = try Core()
        let session = try openExample(core)
        #expect(try core.state(of: session).units == .millimeter)
        try core.apply(session, [.setUnits(.inch)])
        let state = try core.state(of: session)
        #expect(state.units == .inch)
        #expect(state.undoName == "Change units")
        let width = try #require(state.parameters.first { $0.name == "width" })
        #expect(abs(width.value - 10 / 25.4) < 1e-12)
    }
}
