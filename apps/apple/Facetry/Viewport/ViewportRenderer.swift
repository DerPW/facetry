// Swift API over the viewport renderer's C API (render/include/facetry_render.h).
// See docs/adr/0004-viewport-renderer-shared-cpp-sokol.md.

import CFacetryCore
import Metal
import QuartzCore
import SwiftUI

struct RenderError: Error, CustomStringConvertible {
    let status: facetry_render_status
    let message: String

    var description: String { message }
}

/// What a pick or selection refers to.
struct Pick: Equatable, Sendable {
    enum Kind: UInt32, Sendable {
        case none = 0
        case face = 1
        case edge = 2
        case vertex = 3
        /// The shown model is body 1 until there are several (M1.5).
        case body = 4
    }

    var kind: Kind = .none
    var id: UInt32 = 0

    static let none = Pick()
}

/// What clicks and hovering pick (facetry_pick_filter); keys 1–5 like Plasticity's modes.
enum PickFilter: Int, CaseIterable, Identifiable, Sendable {
    case everything = 0
    case vertices = 1
    case edges = 2
    case faces = 3
    case bodies = 4

    var id: Int { rawValue }

    var title: String {
        switch self {
        case .everything: "All"
        case .vertices: "Vertices"
        case .edges: "Edges"
        case .faces: "Faces"
        case .bodies: "Bodies"
        }
    }

    /// The key that chooses it.
    var key: KeyEquivalent {
        switch self {
        case .vertices: "1"
        case .edges: "2"
        case .faces: "3"
        case .bodies: "4"
        case .everything: "5"
        }
    }
}

/// How the model is drawn (facetry_visual_style).
enum VisualStyle: Int, CaseIterable, Identifiable, Sendable {
    case shaded = 0
    case shadedEdges = 1
    case shadedHiddenEdges = 2
    case wireframe = 3

    var id: Int { rawValue }

    var title: String {
        switch self {
        case .shaded: "Shaded"
        case .shadedEdges: "Shaded with edges"
        case .shadedHiddenEdges: "Hidden edges"
        case .wireframe: "Wireframe"
        }
    }
}

/// One cell of the ViewCube (facetry_viewcube_cell): each visible face is split into a center,
/// four edges and four corners.
struct ViewCubeCell: Equatable, Sendable {
    static let faceNames = ["Right", "Left", "Back", "Front", "Top", "Bottom"]

    /// The corners in the cube's square, in points.
    let points: [CGPoint]
    /// Where a click on the cell looks from.
    let direction: SIMD3<Float>
    let face: Int
    /// 0 center, 1 edge, 2 corner.
    let region: Int

    init(_ cell: facetry_viewcube_cell) {
        let p = cell.points
        points = [
            CGPoint(x: Double(p.0), y: Double(p.1)), CGPoint(x: Double(p.2), y: Double(p.3)),
            CGPoint(x: Double(p.4), y: Double(p.5)), CGPoint(x: Double(p.6), y: Double(p.7)),
        ]
        direction = SIMD3(cell.direction.0, cell.direction.1, cell.direction.2)
        face = Int(cell.face)
        region = Int(cell.region)
    }
}

/// The shared C++ viewport renderer on Metal. Only one can exist at a time.
@MainActor
final class ViewportRenderer {
    private let handle: OpaquePointer

    init(device: MTLDevice, sampleCount: Int) throws {
        var created: OpaquePointer?
        let status = facetry_renderer_create_metal(
            Unmanaged.passUnretained(device).toOpaque(), Int32(sampleCount), &created)
        guard status == FACETRY_RENDER_OK, let created else {
            throw RenderError(
                status: status, message: String(cString: facetry_renderer_last_error(nil)))
        }
        handle = created
    }

    // The renderer is not thread-safe: destroy it on the main actor like every other call.
    isolated deinit {
        facetry_renderer_destroy(handle)
    }

    func setMesh(_ mesh: MeshData) throws {
        let status = mesh.positions.withUnsafeBufferPointer { positions in
            mesh.normals.withUnsafeBufferPointer { normals in
                mesh.triangles.withUnsafeBufferPointer { triangles in
                    mesh.triangleFaceIDs.withUnsafeBufferPointer { faceIDs in
                        mesh.edgePoints.withUnsafeBufferPointer { edgePoints in
                            mesh.edgeOffsets.withUnsafeBufferPointer { edgeOffsets in
                                mesh.edgeIDs.withUnsafeBufferPointer { edgeIDs in
                                    mesh.vertexPositions.withUnsafeBufferPointer { vertices in
                                        mesh.vertexIDs.withUnsafeBufferPointer { vertexIDs in
                                            var data = facetry_render_mesh(
                                                positions: positions.baseAddress,
                                                position_count: UInt32(positions.count),
                                                normals: normals.baseAddress,
                                                triangles: triangles.baseAddress,
                                                triangle_index_count: UInt32(triangles.count),
                                                triangle_face_ids: faceIDs.baseAddress,
                                                edge_points: edgePoints.baseAddress,
                                                edge_point_count: UInt32(edgePoints.count),
                                                edge_offsets: edgeOffsets.baseAddress,
                                                edge_offset_count: UInt32(edgeOffsets.count),
                                                edge_ids: edgeIDs.baseAddress,
                                                vertex_positions: vertices.baseAddress,
                                                vertex_position_count: UInt32(vertices.count),
                                                vertex_ids: vertexIDs.baseAddress)
                                            return facetry_renderer_set_mesh(handle, &data)
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        try check(status)
    }

    func setViewport(width: Double, height: Double, pixelRatio: Double) {
        facetry_renderer_set_viewport(handle, Float(width), Float(height), Float(pixelRatio))
    }

    /// Visual style, and the shown body's opacity (1 opaque). In wireframe and with transparent
    /// faces, picking reaches hidden edges and vertices too.
    func setStyle(_ style: VisualStyle, opacity: Double) {
        facetry_renderer_set_style(handle, UInt32(style.rawValue), Float(opacity))
    }

    /// The ground grid; `unitMillimeters` is the size of one document unit (25.4 for inches).
    func setGrid(visible: Bool, unitMillimeters: Double) {
        facetry_renderer_set_grid(handle, visible ? 1 : 0, Float(unitMillimeters))
    }

    /// Frames the whole model from the current direction (F).
    func fit() { facetry_camera_fit(handle) }

    /// Frames the selection, or the whole model (Shift+F).
    func fitSelection() { facetry_camera_fit_selection(handle) }

    /// The home view: the whole model from the front right, above.
    func home(animated: Bool) { facetry_camera_home(handle, animated ? 1 : 0) }

    /// Turns the view to look from `direction` (from the model toward the eye).
    func view(from direction: SIMD3<Float>, animated: Bool) {
        facetry_camera_view_from(handle, direction.x, direction.y, direction.z, animated ? 1 : 0)
    }

    /// The visible ViewCube cells for a cube in a square of `size` points, far to near.
    func viewCubeCells(size: Double) -> [ViewCubeCell] {
        var cells = [facetry_viewcube_cell](repeating: facetry_viewcube_cell(), count: 27)
        let count = Int(facetry_viewcube_cells(handle, Float(size), &cells, UInt32(cells.count)))
        return cells.prefix(min(count, cells.count)).map(ViewCubeCell.init)
    }

    /// Index in ``viewCubeCells(size:)`` of the cell under `point`, or nil.
    func viewCubeHit(size: Double, at point: CGPoint) -> Int? {
        let index = facetry_viewcube_hit(handle, Float(size), Float(point.x), Float(point.y))
        return index < 0 ? nil : Int(index)
    }

    func setOrthographic(_ orthographic: Bool) {
        facetry_camera_set_orthographic(handle, orthographic ? 1 : 0)
    }

    func orbitBegin(at point: CGPoint) {
        facetry_camera_orbit_begin(handle, Float(point.x), Float(point.y))
    }

    func orbit(dx: Double, dy: Double) { facetry_camera_orbit(handle, Float(dx), Float(dy)) }

    func pan(dx: Double, dy: Double) { facetry_camera_pan(handle, Float(dx), Float(dy)) }

    /// factor < 1 zooms in, toward `point`.
    func zoom(factor: Double, at point: CGPoint) {
        facetry_camera_zoom(handle, Float(factor), Float(point.x), Float(point.y))
    }

    func pick(at point: CGPoint, radius: Double) -> Pick {
        var result = facetry_pick_result()
        guard
            facetry_renderer_pick(handle, Float(point.x), Float(point.y), Float(radius), &result)
                == FACETRY_RENDER_OK
        else { return .none }
        return Pick(kind: Pick.Kind(rawValue: result.kind) ?? .none, id: result.id)
    }

    func setHover(_ pick: Pick) { facetry_renderer_set_hover(handle, pick.kind.rawValue, pick.id) }

    func setSelection(_ pick: Pick) {
        facetry_renderer_set_selection(handle, pick.kind.rawValue, pick.id)
    }

    /// Selects only `pick` (none clears), or with `toggle` adds or removes it. Returns the
    /// selection in the order selected.
    func select(_ pick: Pick, toggle: Bool) -> [Pick] {
        facetry_renderer_select(handle, pick.kind.rawValue, pick.id, toggle ? 1 : 0)
        let count = Int(facetry_renderer_selection(handle, nil, 0))
        var pairs = [UInt32](repeating: 0, count: count * 2)
        _ = facetry_renderer_selection(handle, &pairs, UInt32(count))
        return (0..<count).map {
            Pick(kind: Pick.Kind(rawValue: pairs[$0 * 2]) ?? .none, id: pairs[$0 * 2 + 1])
        }
    }

    /// Selects what a click at `point` picks: the first candidate there, or the next one when the
    /// same spot is clicked again (#38); `toggle` adds or removes instead. Returns the picked
    /// element and the selection.
    func click(at point: CGPoint, radius: Double, toggle: Bool) -> (Pick, [Pick]) {
        var result = facetry_pick_result()
        _ = facetry_renderer_click(
            handle, Float(point.x), Float(point.y), Float(radius), toggle ? 1 : 0, &result)
        let pick = Pick(kind: Pick.Kind(rawValue: result.kind) ?? .none, id: result.id)
        return (pick, select(.none, toggle: true))  // reads the selection without changing it
    }

    /// Selects what lies in the box dragged from `start` to `end` (a window to the right, a
    /// crossing box to the left); `add` keeps the current selection.
    func boxSelect(from start: CGPoint, to end: CGPoint, add: Bool) -> [Pick] {
        _ = facetry_renderer_box_select(
            handle, Float(start.x), Float(start.y), Float(end.x), Float(end.y), add ? 1 : 0)
        return select(.none, toggle: true)  // reads the selection without changing it
    }

    func setPickFilter(_ filter: PickFilter) {
        facetry_renderer_set_pick_filter(handle, UInt32(filter.rawValue))
    }

    func draw(drawable: CAMetalDrawable, depthStencil: MTLTexture, multisampleColor: MTLTexture?)
        throws
    {
        var target = facetry_render_target(
            metal_drawable: UnsafeRawPointer(Unmanaged.passUnretained(drawable).toOpaque()),
            metal_depth_stencil_texture: UnsafeRawPointer(
                Unmanaged.passUnretained(depthStencil).toOpaque()),
            metal_msaa_color_texture: multisampleColor.map {
                UnsafeRawPointer(Unmanaged.passUnretained($0).toOpaque())
            })
        try check(facetry_renderer_draw(handle, &target))
    }

    var stats: facetry_render_stats {
        var stats = facetry_render_stats()
        facetry_renderer_stats(handle, &stats)
        return stats
    }

    private func check(_ status: facetry_render_status) throws {
        guard status == FACETRY_RENDER_OK else {
            throw RenderError(
                status: status, message: String(cString: facetry_renderer_last_error(handle)))
        }
    }
}
