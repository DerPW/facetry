// Swift API over the core's C API (core/include/facetry_core.h, imported as CFacetryCore).
// See docs/adr/0003-core-api-boundary-c-abi.md.

import CFacetryCore
import Foundation
import os

/// A failed core call.
struct CoreError: Error, CustomStringConvertible {
    let status: facetry_status
    let message: String

    var description: String { message }
}

/// A shape owned by a ``Core`` context. Topology IDs are only valid for this shape.
struct ShapeHandle: Hashable, Sendable {
    let rawValue: UInt32
}

/// A sketch drawing tool in a ``Core`` context (docs/adr/0015-sketch-tools.md).
struct ToolHandle: Hashable, Sendable {
    let rawValue: UInt32
}

/// The drawing tools (facetry_sketch_tool_kind), with their keys (docs/INPUT.md).
enum SketchToolKind: UInt32, CaseIterable, Identifiable, Sendable {
    case line = 0
    case rectangleTwoPoint = 1
    case rectangleThreePoint = 2
    case rectangleCenter = 3
    case circleCenter = 4
    case circleTwoPoint = 5
    case circleThreePoint = 6
    case arcThreePoint = 7
    case arcCenter = 8
    case arcTangent = 9
    case point = 10

    var id: UInt32 { rawValue }

    var title: String {
        switch self {
        case .line: "Line"
        case .rectangleTwoPoint: "Rectangle: 2 corners"
        case .rectangleThreePoint: "Rectangle: 3 points"
        case .rectangleCenter: "Rectangle: center"
        case .circleCenter: "Circle: center"
        case .circleTwoPoint: "Circle: 2 points"
        case .circleThreePoint: "Circle: 3 points"
        case .arcThreePoint: "Arc: 3 points"
        case .arcCenter: "Arc: center"
        case .arcTangent: "Arc: tangent"
        case .point: "Point"
        }
    }

    /// The key that chooses it (docs/INPUT.md).
    var key: Character? {
        switch self {
        case .line: "l"
        case .rectangleTwoPoint: "r"
        case .circleCenter: "c"
        case .arcThreePoint: "a"
        case .point: "p"
        default: nil
        }
    }
}

/// A pointer event for a sketch tool (facetry_pointer_event).
enum ToolEvent: UInt32, Sendable {
    case down = 0
    case move = 1
    case up = 2
}

enum BooleanOperation {
    case fuse, cut, common

    fileprivate var cValue: facetry_boolean_op {
        switch self {
        case .fuse: FACETRY_BOOLEAN_FUSE
        case .cut: FACETRY_BOOLEAN_CUT
        case .common: FACETRY_BOOLEAN_COMMON
        }
    }
}

struct ShapeProperties: Sendable {
    let volume: Double
    let area: Double
    let boundsMin: SIMD3<Double>
    let boundsMax: SIMD3<Double>
    let solids: Int
    let faces: Int
    let edges: Int
    let vertices: Int
}

/// Display mesh, laid out for direct GPU upload (see facetry_mesh_buffer_kind).
struct MeshData: Sendable, Equatable {
    var positions: [Float] = []
    var normals: [Float] = []
    var triangles: [UInt32] = []
    var triangleFaceIDs: [UInt32] = []
    var edgePoints: [Float] = []
    var edgeOffsets: [UInt32] = []
    var edgeIDs: [UInt32] = []
    var vertexPositions: [Float] = []
    var vertexIDs: [UInt32] = []

    var triangleCount: Int { triangles.count / 3 }

    /// Nothing to show. Edge offsets always have one entry more than there are edges.
    static let empty = MeshData(edgeOffsets: [0])
}

/// An open document in a ``Core`` context: description, undo/redo and regeneration cache.
struct SessionHandle: Hashable, Sendable {
    let rawValue: UInt32
}

enum ElementKind: Sendable {
    case face, edge, vertex

    fileprivate var cValue: facetry_element_kind {
        switch self {
        case .face: FACETRY_ELEMENT_FACE
        case .edge: FACETRY_ELEMENT_EDGE
        case .vertex: FACETRY_ELEMENT_VERTEX
        }
    }
}

/// Summary of a session (facetry_session_state; the JSON is described in
/// core/include/facetry/core/session_json.hpp).
/// The unit of a document's lengths. Parameters and feature values are in it; the core builds
/// geometry in millimeters.
enum LengthUnit: String, Codable, Sendable, CaseIterable, Identifiable {
    case millimeter = "mm"
    case centimeter = "cm"
    case meter = "m"
    case inch = "in"

    var id: String { rawValue }
    var symbol: String { rawValue }

    var millimeters: Double {
        switch self {
        case .millimeter: 1
        case .centimeter: 10
        case .meter: 1000
        case .inch: 25.4
        }
    }
}

struct SessionState: Decodable, Sendable, Equatable {
    struct Parameter: Decodable, Sendable, Equatable, Identifiable {
        let name: String
        let value: Double

        var id: String { name }
    }

    struct Feature: Decodable, Sendable, Equatable, Identifiable {
        let id: String
        let name: String
        let type: String
        let suppressed: Bool
        /// "ok", "warning", "error", "suppressed", "rolledBack" or "notRun".
        let status: String
        let messages: [String]
        /// Indices into the feature's references (a fillet's `edges`) that could not be resolved.
        let brokenReferences: [Int]
        /// Later features that use this one, directly or through others.
        let dependents: [String]
        /// Sketches: the names of their closed regions (profiles), as of the last regeneration.
        /// An extrusion names the ones it takes in its definition's "regions".
        let regions: [String]?
        /// Sketches: where they lie in space, or nil before they regenerated.
        let frame: SketchFrame?
        /// The feature in the document's JSON form, to change and send back with
        /// ``DocumentEdit/replaceFeature(_:)``.
        let definition: JSONValue

        /// A sketch's curves in the order of its display mesh (edge ID i + 1 is curve i): its
        /// lines, then arcs, then circles, as in its definition.
        var sketchCurves: [(id: String, construction: Bool)] {
            ["lines", "arcs", "circles"].flatMap { key -> [(id: String, construction: Bool)] in
                guard case .array(let curves)? = definition[key] else { return [] }
                return curves.compactMap { curve in
                    guard let id = curve["id"]?.string else { return nil }
                    return (id, curve["construction"] == .bool(true))
                }
            }
        }
    }

    /// Where a sketch lies: its origin and unit axes in millimeters; `normal` is x × y.
    struct SketchFrame: Decodable, Sendable, Equatable {
        let origin: [Double]
        let xAxis: [Double]
        let yAxis: [Double]
        let normal: [Double]
    }

    struct Regeneration: Decodable, Sendable, Equatable {
        let cancelled: Bool
        let executed: Int
    }

    let units: LengthUnit
    let parameters: [Parameter]
    let rollback: Int?
    let canUndo: Bool
    let canRedo: Bool
    /// Names of the steps undo and redo would take back or repeat, e.g. "Rename feature".
    let undoName: String?
    let redoName: String?
    /// A transaction (one undo step for a whole tool) is open.
    let inTransaction: Bool
    /// Number of steps undo can take back.
    let undoCount: Int
    let needsRegeneration: Bool
    let features: [Feature]
    /// Bodies of the last regeneration; a body's ID is the ID of the feature that created it.
    let bodies: [Body]
    /// The fixed origin: base planes, axes and the origin point.
    let origin: [OriginItem]
    let regeneration: Regeneration

    struct Body: Decodable, Sendable, Equatable, Identifiable {
        let id: String
        let name: String
        let visible: Bool
        /// 1 opaque, down to 0.
        let opacity: Double
    }

    struct OriginItem: Decodable, Sendable, Equatable, Identifiable {
        let id: String
        let name: String
        /// "plane", "axis" or "point".
        let kind: String
        let visible: Bool
    }
}

/// Persistent names of a body's elements; index i belongs to element ID i + 1.
struct ElementNames: Decodable, Sendable, Equatable {
    let faces: [String]
    let edges: [String]
    let vertices: [String]
}

/// Any JSON value. Feature definitions pass through the app in this form until the app has its own
/// feature types.
enum JSONValue: Codable, Sendable, Equatable {
    case null
    case bool(Bool)
    case number(Double)
    case string(String)
    case array([JSONValue])
    case object([String: JSONValue])

    init(from decoder: any Decoder) throws {
        let c = try decoder.singleValueContainer()
        if c.decodeNil() {
            self = .null
        } else if let value = try? c.decode(Bool.self) {
            self = .bool(value)
        } else if let value = try? c.decode(Double.self) {
            self = .number(value)
        } else if let value = try? c.decode(String.self) {
            self = .string(value)
        } else if let value = try? c.decode([JSONValue].self) {
            self = .array(value)
        } else {
            self = .object(try c.decode([String: JSONValue].self))
        }
    }

    func encode(to encoder: any Encoder) throws {
        var c = encoder.singleValueContainer()
        switch self {
        case .null: try c.encodeNil()
        case .bool(let value): try c.encode(value)
        case .number(let value): try c.encode(value)
        case .string(let value): try c.encode(value)
        case .array(let value): try c.encode(value)
        case .object(let value): try c.encode(value)
        }
    }

    subscript(key: String) -> JSONValue? {
        get {
            guard case .object(let object) = self else { return nil }
            return object[key]
        }
        set {
            guard case .object(var object) = self else { return }
            object[key] = newValue
            self = .object(object)
        }
    }

    subscript(index: Int) -> JSONValue? {
        guard case .array(let array) = self, array.indices.contains(index) else { return nil }
        return array[index]
    }

    var number: Double? {
        if case .number(let value) = self { value } else { nil }
    }

    var string: String? {
        if case .string(let value) = self { value } else { nil }
    }
}

/// An edit for ``Core/apply(_:_:)`` (the JSON forms are in core/include/facetry/core/session_json.hpp).
enum DocumentEdit: Encodable, Sendable, Equatable {
    case setParameter(name: String, value: Double)
    case addParameter(name: String, value: Double)
    case deleteParameter(name: String)
    case deleteFeature(id: String)
    case renameFeature(id: String, name: String)
    case suppressFeature(id: String, suppressed: Bool)
    case setRollback(index: Int?)
    /// Replaces the feature with the same `id` (a changed ``SessionState/Feature/definition``).
    case replaceFeature(JSONValue)
    /// Adds a feature (a definition without `id`) at `index` in the timeline, or at the end.
    case addFeature(JSONValue, index: Int?)
    /// Makes curves of a sketch construction curves, or normal ones again.
    case setConstruction(sketch: String, entities: [String], construction: Bool)
    /// An empty name restores the default ("Body 1", …).
    case renameBody(id: String, name: String)
    case setBodyVisible(id: String, visible: Bool)
    case setOriginVisible(id: String, visible: Bool)
    /// 1 opaque, down to 0.
    case setBodyOpacity(id: String, opacity: Double)
    /// Converts every length, so the geometry stays the same.
    case setUnits(LengthUnit)

    private enum Key: String, CodingKey {
        case op, name, value, id, suppressed, index, feature, visible, units, opacity, sketch
        case entities, construction
    }

    func encode(to encoder: any Encoder) throws {
        var c = encoder.container(keyedBy: Key.self)
        switch self {
        case .setParameter(let name, let value), .addParameter(let name, let value):
            try c.encode(self.op, forKey: .op)
            try c.encode(name, forKey: .name)
            try c.encode(value, forKey: .value)
        case .deleteParameter(let name):
            try c.encode(op, forKey: .op)
            try c.encode(name, forKey: .name)
        case .deleteFeature(let id):
            try c.encode(op, forKey: .op)
            try c.encode(id, forKey: .id)
        case .renameFeature(let id, let name):
            try c.encode(op, forKey: .op)
            try c.encode(id, forKey: .id)
            try c.encode(name, forKey: .name)
        case .suppressFeature(let id, let suppressed):
            try c.encode(op, forKey: .op)
            try c.encode(id, forKey: .id)
            try c.encode(suppressed, forKey: .suppressed)
        case .setRollback(let index):
            try c.encode(op, forKey: .op)
            if let index {
                try c.encode(index, forKey: .index)
            } else {
                try c.encodeNil(forKey: .index)
            }
        case .replaceFeature(let definition):
            try c.encode(op, forKey: .op)
            try c.encode(definition, forKey: .feature)
        case .addFeature(let definition, let index):
            try c.encode(op, forKey: .op)
            try c.encode(definition, forKey: .feature)
            try c.encodeIfPresent(index, forKey: .index)
        case .setConstruction(let sketch, let entities, let construction):
            try c.encode(op, forKey: .op)
            try c.encode(sketch, forKey: .sketch)
            try c.encode(entities, forKey: .entities)
            try c.encode(construction, forKey: .construction)
        case .renameBody(let id, let name):
            try c.encode(op, forKey: .op)
            try c.encode(id, forKey: .id)
            try c.encode(name, forKey: .name)
        case .setBodyVisible(let id, let visible), .setOriginVisible(let id, let visible):
            try c.encode(op, forKey: .op)
            try c.encode(id, forKey: .id)
            try c.encode(visible, forKey: .visible)
        case .setBodyOpacity(let id, let opacity):
            try c.encode(op, forKey: .op)
            try c.encode(id, forKey: .id)
            try c.encode(opacity, forKey: .opacity)
        case .setUnits(let units):
            try c.encode(op, forKey: .op)
            try c.encode(units, forKey: .units)
        }
    }

    private var op: String {
        switch self {
        case .setParameter: "setParameter"
        case .addParameter: "addParameter"
        case .deleteParameter: "deleteParameter"
        case .deleteFeature: "deleteFeature"
        case .renameFeature: "renameFeature"
        case .suppressFeature: "suppressFeature"
        case .setRollback: "setRollback"
        case .replaceFeature: "replaceFeature"
        case .addFeature: "addFeature"
        case .setConstruction: "setConstruction"
        case .renameBody: "renameBody"
        case .setBodyVisible: "setBodyVisible"
        case .setOriginVisible: "setOriginVisible"
        case .setBodyOpacity: "setBodyOpacity"
        case .setUnits: "setUnits"
        }
    }
}

struct CoreVersions: Sendable {
    let apiMajor: UInt32
    let apiMinor: UInt32
    let core: String
    let kernel: String
    let platform: String

    static var current: CoreVersions {
        var major: UInt32 = 0
        var minor: UInt32 = 0
        facetry_api_version(&major, &minor)
        return CoreVersions(
            apiMajor: major,
            apiMinor: minor,
            core: String(cString: facetry_core_version()),
            kernel: String(cString: facetry_kernel_version()),
            platform: String(cString: facetry_platform()))
    }
}

/// One core context. It owns every shape it creates; all of them are freed with the context.
/// Calls are synchronous, so run long operations off the main thread.
final class Core {
    /// The C API major version this wrapper was written for.
    static let supportedAPIMajor: UInt32 = 0

    private let context: OpaquePointer

    init() throws {
        let versions = CoreVersions.current
        guard versions.apiMajor == Self.supportedAPIMajor else {
            throw CoreError(
                status: FACETRY_ERROR_UNSUPPORTED,
                message: "Core API \(versions.apiMajor).x does not match the app "
                    + "(expects \(Self.supportedAPIMajor).x)")
        }
        var created: OpaquePointer?
        let status = facetry_context_create(&created)
        guard status == FACETRY_OK, let created else {
            throw CoreError(status: status, message: "Could not create a core context")
        }
        context = created
        Self.routeLogsToOSLog()
    }

    deinit {
        facetry_context_destroy(context)
    }

    func makeBox(_ dx: Double, _ dy: Double, _ dz: Double) throws -> ShapeHandle {
        try makeShape { facetry_shape_make_box(context, dx, dy, dz, $0) }
    }

    func makeCylinder(x: Double, y: Double, radius: Double, height: Double) throws -> ShapeHandle {
        try makeShape { facetry_shape_make_cylinder(context, x, y, radius, height, $0) }
    }

    func boolean(_ operation: BooleanOperation, _ shape: ShapeHandle, _ tool: ShapeHandle) throws
        -> ShapeHandle
    {
        try makeShape {
            facetry_shape_boolean(context, operation.cValue, shape.rawValue, tool.rawValue, $0)
        }
    }

    func fillet(_ shape: ShapeHandle, edges: [UInt32], radius: Double) throws -> ShapeHandle {
        try edges.withUnsafeBufferPointer { ids in
            try makeShape {
                facetry_shape_fillet(
                    context, shape.rawValue, ids.baseAddress, ids.count, radius, $0)
            }
        }
    }

    func properties(of shape: ShapeHandle) throws -> ShapeProperties {
        var p = facetry_shape_properties()
        try check(facetry_shape_properties_get(context, shape.rawValue, &p))
        return ShapeProperties(
            volume: p.volume,
            area: p.area,
            boundsMin: SIMD3(p.bbox_min.0, p.bbox_min.1, p.bbox_min.2),
            boundsMax: SIMD3(p.bbox_max.0, p.bbox_max.1, p.bbox_max.2),
            solids: Int(p.solid_count),
            faces: Int(p.face_count),
            edges: Int(p.edge_count),
            vertices: Int(p.vertex_count))
    }

    func tessellate(
        _ shape: ShapeHandle, linearDeflection: Double = 0.05, angularDeflection: Double = 0.35,
        relative: Bool = false
    ) throws -> MeshData {
        var mesh: facetry_handle = 0
        try check(
            facetry_shape_tessellate(
                context, shape.rawValue, linearDeflection, angularDeflection, relative ? 1 : 0,
                &mesh))
        return try takeMesh(mesh)
    }

    /// A sketch for display: edge ID i + 1 is its curve i (see
    /// ``SessionState/Feature/sketchCurves``), sampled within `deflection` millimeters; vertex ID
    /// i + 1 is its point i.
    func sketchMesh(_ session: SessionHandle, sketch: String, deflection: Double = 0.01) throws
        -> MeshData
    {
        var mesh: facetry_handle = 0
        try check(
            facetry_session_sketch_mesh(context, session.rawValue, sketch, deflection, &mesh))
        return try takeMesh(mesh)
    }

    // MARK: - Sketch tools

    /// A drawing tool for the sketch feature `sketch`. Release it when done.
    func createSketchTool(sketch: String, kind: SketchToolKind, construction: Bool) throws
        -> ToolHandle
    {
        var handle: facetry_handle = 0
        try check(
            facetry_sketch_tool_create(
                context, sketch, kind.rawValue, construction ? 1 : 0, &handle))
        return ToolHandle(rawValue: handle)
    }

    /// A pointer event at (u, v) on the sketch plane in millimeters; positions within
    /// `snapRadius` millimeters of a sketch point use it. True if the document changed.
    func sketchToolPointer(
        _ session: SessionHandle, tool: ToolHandle, event: ToolEvent, u: Double, v: Double,
        snapRadius: Double
    ) throws -> Bool {
        var changed: Int32 = 0
        try check(
            facetry_sketch_tool_pointer(
                context, session.rawValue, tool.rawValue, event.rawValue, u, v, snapRadius,
                &changed))
        return changed != 0
    }

    /// Esc: drops the shape in progress. False if nothing was in progress.
    func cancelSketchTool(_ tool: ToolHandle) throws -> Bool {
        var busy: Int32 = 0
        try check(facetry_sketch_tool_cancel(context, tool.rawValue, &busy))
        return busy != 0
    }

    /// What the tool would draw next, like ``sketchMesh(_:sketch:deflection:)``.
    func sketchToolPreview(_ session: SessionHandle, tool: ToolHandle) throws -> MeshData {
        var mesh: facetry_handle = 0
        try check(
            facetry_sketch_tool_preview(context, session.rawValue, tool.rawValue, 0.01, &mesh))
        return try takeMesh(mesh)
    }

    func releaseSketchTool(_ tool: ToolHandle) throws {
        try check(facetry_sketch_tool_release(context, tool.rawValue))
    }

    /// Copies a mesh out of the core and releases it.
    private func takeMesh(_ mesh: facetry_handle) throws -> MeshData {
        defer { facetry_mesh_release(context, mesh) }

        var data = MeshData()
        data.positions = try buffer(mesh, FACETRY_MESH_POSITIONS)
        data.normals = try buffer(mesh, FACETRY_MESH_NORMALS)
        data.triangles = try buffer(mesh, FACETRY_MESH_TRIANGLES)
        data.triangleFaceIDs = try buffer(mesh, FACETRY_MESH_TRIANGLE_FACE_IDS)
        data.edgePoints = try buffer(mesh, FACETRY_MESH_EDGE_POINTS)
        data.edgeOffsets = try buffer(mesh, FACETRY_MESH_EDGE_OFFSETS)
        data.edgeIDs = try buffer(mesh, FACETRY_MESH_EDGE_IDS)
        data.vertexPositions = try buffer(mesh, FACETRY_MESH_VERTEX_POSITIONS)
        data.vertexIDs = try buffer(mesh, FACETRY_MESH_VERTEX_IDS)
        return data
    }

    func release(_ shape: ShapeHandle) throws {
        try check(facetry_shape_release(context, shape.rawValue))
    }

    // MARK: - Documents

    /// Opens a document from its JSON description, or an empty document.
    func openSession(json: String? = nil) throws -> SessionHandle {
        var handle: facetry_handle = 0
        try check(facetry_session_create(context, json, &handle))
        return SessionHandle(rawValue: handle)
    }

    /// The document's JSON description.
    func save(_ session: SessionHandle) throws -> String {
        try string { facetry_session_save(context, session.rawValue, $0) }
    }

    /// Opens a .facetry file (or a bare document.json, or a provisional M0.5 file).
    func openSession(fileData: Data) throws -> SessionHandle {
        var handle: facetry_handle = 0
        try fileData.withUnsafeBytes { bytes in
            try check(
                facetry_session_open_file(
                    context, bytes.bindMemory(to: UInt8.self).baseAddress, bytes.count, &handle))
        }
        return SessionHandle(rawValue: handle)
    }

    /// The document as a .facetry file, with an optional PNG thumbnail.
    func saveFile(_ session: SessionHandle, thumbnail: Data? = nil) throws -> Data {
        var data: UnsafePointer<UInt8>?
        var size = 0
        try (thumbnail ?? Data()).withUnsafeBytes { png in
            try check(
                facetry_session_save_file(
                    context, session.rawValue, png.bindMemory(to: UInt8.self).baseAddress,
                    png.count, &data, &size))
        }
        guard let data else { return Data() }
        return Data(bytes: data, count: size)
    }

    /// Applies edits as one undo step. A failing edit changes nothing.
    func apply(_ session: SessionHandle, _ edits: [DocumentEdit]) throws {
        let json = String(decoding: try JSONEncoder().encode(edits), as: UTF8.self)
        try apply(session, json: json)
    }

    /// Applies one edit or a JSON array of edits (see core/include/facetry/core/session_json.hpp).
    func apply(_ session: SessionHandle, json: String) throws {
        try check(facetry_session_apply(context, session.rawValue, json))
    }

    /// Groups the following edits into one undo step until ``endTransaction(_:commit:)``.
    func beginTransaction(_ session: SessionHandle, name: String) throws {
        try check(facetry_session_begin_transaction(context, session.rawValue, name))
    }

    /// Commits the open transaction as one undo step, or cancels it (restoring the document).
    func endTransaction(_ session: SessionHandle, commit: Bool) throws {
        try check(facetry_session_end_transaction(context, session.rawValue, commit ? 1 : 0))
    }

    /// Returns false if there was nothing to undo.
    @discardableResult
    func undo(_ session: SessionHandle) throws -> Bool {
        var changed: Int32 = 0
        try check(facetry_session_undo(context, session.rawValue, &changed))
        return changed != 0
    }

    /// Returns false if there was nothing to redo.
    @discardableResult
    func redo(_ session: SessionHandle) throws -> Bool {
        var changed: Int32 = 0
        try check(facetry_session_redo(context, session.rawValue, &changed))
        return changed != 0
    }

    /// Regenerates the document. `progress` is called after each feature with the features done
    /// and the total, and with the same count while a long kernel operation runs; returning false
    /// cancels the rest. Feature failures do not throw; they are reported in ``state(of:)``.
    func regenerate(_ session: SessionHandle, progress: ((Int, Int) -> Bool)? = nil) throws {
        guard let progress else {
            try check(facetry_session_regenerate(context, session.rawValue, nil, nil))
            return
        }
        let box = ProgressBox(progress)
        try withExtendedLifetime(box) {
            try check(
                facetry_session_regenerate(
                    context, session.rawValue,
                    { done, total, data in
                        guard let data else { return 0 }
                        let box = Unmanaged<ProgressBox>.fromOpaque(data).takeUnretainedValue()
                        return box.handler(Int(done), Int(total)) ? 0 : 1
                    }, Unmanaged.passUnretained(box).toOpaque()))
        }
    }

    func state(of session: SessionHandle) throws -> SessionState {
        let json = try string { facetry_session_state(context, session.rawValue, $0) }
        return try JSONDecoder().decode(SessionState.self, from: Data(json.utf8))
    }

    /// A new shape handle for a body of the last regeneration. Release it when done.
    func bodyShape(_ session: SessionHandle, body: String) throws -> ShapeHandle {
        try makeShape { facetry_session_body_shape(context, session.rawValue, body, $0) }
    }

    /// A new shape handle for a sketch's regions, face ID i + 1 being region i. Release it when
    /// done.
    func sketchRegionsShape(_ session: SessionHandle, sketch: String) throws -> ShapeHandle {
        try makeShape { facetry_session_sketch_regions(context, session.rawValue, sketch, $0) }
    }

    func elementNames(_ session: SessionHandle, body: String) throws -> ElementNames {
        let json = try string { facetry_session_element_names(context, session.rawValue, body, $0) }
        return try JSONDecoder().decode(ElementNames.self, from: Data(json.utf8))
    }

    /// A persistent reference to a picked element, as JSON for feature edits.
    func reference(
        in session: SessionHandle, body: String, kind: ElementKind, id: UInt32
    ) throws -> String {
        try string {
            facetry_session_make_reference(context, session.rawValue, body, kind.cValue, id, $0)
        }
    }

    func release(_ session: SessionHandle) throws {
        try check(facetry_session_release(context, session.rawValue))
    }

    // MARK: - Private

    private func makeShape(_ call: (UnsafeMutablePointer<facetry_handle>) -> facetry_status) throws
        -> ShapeHandle
    {
        var handle: facetry_handle = 0
        try check(call(&handle))
        return ShapeHandle(rawValue: handle)
    }

    /// Calls a function that returns a string owned by the context, and copies it.
    private func string(_ call: (UnsafeMutablePointer<UnsafePointer<CChar>?>) -> facetry_status)
        throws -> String
    {
        var text: UnsafePointer<CChar>?
        try check(call(&text))
        return text.map { String(cString: $0) } ?? ""
    }

    private func buffer<T>(_ mesh: facetry_handle, _ kind: facetry_mesh_buffer_kind) throws -> [T] {
        var data: UnsafeRawPointer?
        var count = 0
        try check(facetry_mesh_buffer(context, mesh, kind, &data, &count))
        guard let data, count > 0 else { return [] }
        return Array(UnsafeBufferPointer(start: data.assumingMemoryBound(to: T.self), count: count))
    }

    private func check(_ status: facetry_status) throws {
        guard status == FACETRY_OK else {
            throw CoreError(
                status: status, message: String(cString: facetry_last_error_message(context)))
        }
    }

    private static let logRouting: Void = {
        facetry_set_log_callback(
            { level, message, _ in
                let text = message.map { String(cString: $0) } ?? ""
                switch level {
                case FACETRY_LOG_ERROR: coreLogger.error("\(text, privacy: .public)")
                case FACETRY_LOG_WARNING: coreLogger.warning("\(text, privacy: .public)")
                case FACETRY_LOG_INFO: coreLogger.info("\(text, privacy: .public)")
                default: coreLogger.debug("\(text, privacy: .public)")
                }
            }, nil)
    }()

    /// Sends core log messages to the unified log (subsystem io.github.derpw.facetry, category core).
    static func routeLogsToOSLog() {
        _ = logRouting
    }
}

/// Carries a progress closure through the C API's `void*` user data.
private final class ProgressBox {
    let handler: (Int, Int) -> Bool

    init(_ handler: @escaping (Int, Int) -> Bool) {
        self.handler = handler
    }
}

private let coreLogger = Logger(subsystem: "io.github.derpw.facetry", category: "core")
