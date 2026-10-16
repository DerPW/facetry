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

    // MARK: - Private

    private func makeShape(_ call: (UnsafeMutablePointer<facetry_handle>) -> facetry_status) throws
        -> ShapeHandle
    {
        var handle: facetry_handle = 0
        try check(call(&handle))
        return ShapeHandle(rawValue: handle)
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

private let coreLogger = Logger(subsystem: "io.github.derpw.facetry", category: "core")
