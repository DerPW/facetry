// SwiftUI host for the Metal viewport: an MTKView that renders with the shared C++ renderer and
// turns mouse, trackpad, touch and Pencil input into camera moves, hover and selection.

import MetalKit
import SwiftUI
import os

private let viewportLogger = Logger(subsystem: "io.github.derpw.facetry", category: "viewport")

/// State the viewport shares with the rest of the UI.
@MainActor
@Observable
final class ViewportModel {
    /// The mesh to show. The first mesh is framed; later ones keep the camera (call ``fit()``).
    var mesh: MeshData?
    var orthographic = false
    /// The element picked last (for repairs); set it to `.none` to clear the selection.
    var selection = Pick.none
    /// Everything selected, in the order selected.
    fileprivate(set) var selected: [Pick] = []
    /// What clicks pick.
    var pickFilter = PickFilter.everything
    /// Every tap or click adds to the selection or removes from it (for touch, without keys).
    var multiSelect = false
    /// Drags draw selection boxes (for the Pencil and fingers; the Fusion preset's left drag
    /// always does).
    var boxMode = false
    /// A sketch's regions to choose profiles from, shown over the model. While they are shown,
    /// clicks and taps pick only regions and add or remove them.
    var regions: RegionChoice?
    /// The origin planes to start a sketch on (#61), shown while the sketch plane is picked.
    var originPlanes = false
    /// The sketch being edited, drawn over everything.
    var sketch: SketchDisplay?
    /// What the sketch tool would draw next.
    var sketchPreview: MeshData?
    /// While set, the left button, the Pencil and one finger draw with a sketch tool instead of
    /// selecting or orbiting (docs/INPUT.md).
    @ObservationIgnored var drawing: DrawingInput?
    /// What the pointer snapped to while drawing, shown next to it.
    var pointerLabel = ""
    fileprivate(set) var pointerLabelAt: CGPoint?
    /// Labels on the sketch (constraint glyphs) at points in space, and where they show on
    /// screen, kept current by the viewport.
    var glyphs: [GlyphLabel] = []
    fileprivate(set) var placedGlyphs: [PlacedGlyph] = []
    /// The selection box being dragged, in view points; drawn by ``SelectionBoxOverlay``.
    fileprivate(set) var selectionBox: (start: CGPoint, end: CGPoint)?
    /// Where the precision selector's crosshair points (#38), above the finger.
    fileprivate(set) var crosshair: CGPoint?
    /// Mouse and trackpad preset; the app stores it per device.
    var navigation = NavigationPreset.facetry
    /// One finger pans and two fingers orbit, instead of the other way round.
    var oneFingerPans = false
    /// Visual style (a device setting) and the shown body's opacity (from the document).
    var style = VisualStyle.shadedEdges
    var opacity = 1.0
    /// The ground grid, and the size of one document unit in millimeters for its spacing.
    var grid = true
    var gridUnitMillimeters = 1.0
    /// Frame statistics, updated about once per second.
    var frameMilliseconds = 0.0
    var drawMilliseconds = 0.0
    var triangles = 0
    var error: String?

    /// The ViewCube's visible cells, kept current by the viewport (see ViewCubeView).
    fileprivate(set) var viewCube: [ViewCubeCell] = []

    fileprivate var fitRequests = 0
    @ObservationIgnored fileprivate weak var coordinator: ViewportCoordinator?

    func fit() { fitRequests += 1 }

    /// Frames the selection, or the whole model.
    func fitSelection() { coordinator?.renderer?.fitSelection() }

    /// Selects the shown body (from the browser panel).
    func selectBody() { coordinator?.select(Pick(kind: .body, id: 1), toggle: false) }

    /// The home view: the whole model from the front right, above.
    func home() { coordinator?.renderer?.home(animated: true) }

    /// Turns the view to look from `direction` (from the model toward the eye).
    func view(from direction: SIMD3<Float>) {
        coordinator?.renderer?.view(from: direction, animated: true)
    }

    /// The ViewCube cell under `point` in its square, if any.
    func viewCubeCell(at point: CGPoint) -> ViewCubeCell? {
        guard let renderer = coordinator?.renderer,
            let index = renderer.viewCubeHit(size: ViewCubeView.size, at: point)
        else { return nil }
        let cells = renderer.viewCubeCells(size: ViewCubeView.size)
        return cells.indices.contains(index) ? cells[index] : nil
    }

    /// Orbits around the point in the middle of the view (dragging the ViewCube).
    func orbitFromCenter(dx: Double, dy: Double, begin: Bool) {
        guard let coordinator, let renderer = coordinator.renderer else { return }
        if begin { renderer.orbitBegin(at: coordinator.viewCenter) }
        renderer.orbit(dx: dx, dy: dy)
    }
}

/// Pointer input for a sketch drawing tool: events on the sketch plane, in millimeters along its
/// axes, with the snap radius for the pointer and the grid's spacing there.
struct DrawingInput {
    let plane: SketchPlane
    let onPointer:
        (_ event: ToolEvent, _ u: Double, _ v: Double, _ snapRadius: Double, _ grid: Double) -> Void
}

/// A label on the sketch at a point in space (millimeters).
struct GlyphLabel: Equatable {
    let text: String
    let at: SIMD3<Float>
}

/// A label where it shows on screen.
struct PlacedGlyph: Equatable {
    let text: String
    let point: CGPoint
}

/// A sketch for display: its mesh and the edge IDs of its construction curves (dashed).
struct SketchDisplay: Equatable {
    let mesh: MeshData
    let construction: [UInt32]
    let projected: [UInt32]
}

/// Regions to choose from: a mesh of a sketch's region shape and the region IDs selected first.
struct RegionChoice: Equatable {
    let mesh: MeshData
    let selected: [UInt32]
}

/// Pick radius in points per input type: fingers need more room than the Pencil or a mouse.
private enum PickRadius {
    static let mouse = 6.0
    static let pencil = 8.0
    static let finger = 18.0
}

@MainActor
final class ViewportCoordinator: NSObject, MTKViewDelegate {
    let model: ViewportModel
    var renderer: ViewportRenderer?
    private var shownMesh: MeshData?
    private var shownFitRequests = 0
    private var shownOrthographic = false
    private var shownSelection = Pick.none
    private var shownPickFilter: (filter: PickFilter, regionsOnly: Bool)?
    private var shownRegions: RegionChoice?
    private var shownOriginPlanes = false
    private var shownSketch: SketchDisplay?
    private var shownPreview: MeshData?
    private var shownGrid: (visible: Bool, unit: Double)?
    private var shownStyle: (style: VisualStyle, opacity: Double)?
    private var lastFrameTime: CFTimeInterval = 0
    private var frameTimes: [Double] = []

    /// Middle of the view, in points.
    fileprivate(set) var viewCenter = CGPoint.zero

    init(model: ViewportModel) {
        self.model = model
        super.init()
        model.coordinator = self
    }

    func configure(_ view: MTKView) {
        view.device = MTLCreateSystemDefaultDevice()
        view.colorPixelFormat = .bgra8Unorm
        view.depthStencilPixelFormat = .depth32Float_stencil8
        view.sampleCount = 4
        view.preferredFramesPerSecond = 120
        view.delegate = self
        guard let device = view.device else {
            model.error = "Metal is not available"
            return
        }
        do {
            renderer = try ViewportRenderer(device: device, sampleCount: view.sampleCount)
        } catch {
            model.error = "\(error)"
        }
        updateViewport(view)
    }

    /// Applies model changes (mesh, projection, fit) to the renderer.
    func sync() {
        guard let renderer else { return }
        if let mesh = model.mesh, mesh.positions != shownMesh?.positions {
            do {
                try renderer.setMesh(mesh)
                if shownMesh == nil { renderer.home(animated: false) }
                model.triangles = mesh.triangleCount
                // The renderer cleared the selection: element IDs change with the geometry.
                model.selected = []
                model.selection = .none
                shownSelection = .none
                // The regions' first selection is applied again.
                shownRegions = nil
            } catch {
                model.error = "\(error)"
            }
            shownMesh = mesh
        }
        if model.orthographic != shownOrthographic {
            renderer.setOrthographic(model.orthographic)
            shownOrthographic = model.orthographic
        }
        // The selection only changes here when someone clears it (Esc, a new mesh).
        if model.selection != shownSelection {
            if model.selection == .none { select(.none, toggle: false) }
            shownSelection = model.selection
        }
        if model.regions != shownRegions {
            showRegions(model.regions, on: renderer)
        }
        if model.originPlanes != shownOriginPlanes {
            do {
                try renderer.setOriginPlanes(model.originPlanes)
            } catch {
                model.error = "\(error)"
            }
            shownOriginPlanes = model.originPlanes
        }
        if model.sketch != shownSketch {
            do {
                try renderer.setSketch(
                    model.sketch?.mesh, construction: model.sketch?.construction ?? [],
                    projected: model.sketch?.projected ?? [])
            } catch {
                model.error = "\(error)"
            }
            shownSketch = model.sketch
        }
        if model.sketchPreview != shownPreview {
            do {
                try renderer.setSketchPreview(model.sketchPreview)
            } catch {
                model.error = "\(error)"
            }
            shownPreview = model.sketchPreview
        }
        let regionsOnly = model.regions != nil
        if shownPickFilter?.filter != model.pickFilter
            || shownPickFilter?.regionsOnly != regionsOnly
        {
            if regionsOnly {
                renderer.pickRegionsOnly()
            } else {
                renderer.setPickFilter(model.pickFilter)
            }
            shownPickFilter = (model.pickFilter, regionsOnly)
        }
        if shownStyle?.style != model.style || shownStyle?.opacity != model.opacity {
            renderer.setStyle(model.style, opacity: model.opacity)
            shownStyle = (model.style, model.opacity)
        }
        if shownGrid?.visible != model.grid || shownGrid?.unit != model.gridUnitMillimeters {
            renderer.setGrid(visible: model.grid, unitMillimeters: model.gridUnitMillimeters)
            shownGrid = (model.grid, model.gridUnitMillimeters)
        }
        if model.fitRequests != shownFitRequests {
            renderer.fit()
            shownFitRequests = model.fitRequests
        }
    }

    private func showRegions(_ regions: RegionChoice?, on renderer: ViewportRenderer) {
        do {
            try renderer.setRegions(regions?.mesh)
        } catch {
            model.error = "\(error)"
        }
        var selected = renderer.select(.none, toggle: true)  // reads the selection
        for id in regions?.selected ?? [] {
            selected = renderer.select(Pick(kind: .region, id: id), toggle: true)
        }
        model.selected = selected
        shownRegions = regions
    }

    func updateViewport(_ view: MTKView) {
        viewCenter = CGPoint(x: view.bounds.midX, y: view.bounds.midY)
        let scale = view.drawableSize.width / max(view.bounds.width, 1)
        renderer?.setViewport(
            width: view.bounds.width, height: view.bounds.height, pixelRatio: scale)
    }

    nonisolated func mtkView(_ view: MTKView, drawableSizeWillChange size: CGSize) {
        MainActor.assumeIsolated { updateViewport(view) }
    }

    nonisolated func draw(in view: MTKView) {
        MainActor.assumeIsolated { render(view) }
    }

    private func render(_ view: MTKView) {
        sync()
        guard let renderer, let drawable = view.currentDrawable,
            let depth = view.depthStencilTexture
        else { return }
        do {
            try renderer.draw(
                drawable: drawable, depthStencil: depth,
                multisampleColor: view.multisampleColorTexture
            )
        } catch {
            model.error = "\(error)"
        }
        // The ViewCube and the glyphs follow the camera, including its animations.
        let cells = renderer.viewCubeCells(size: ViewCubeView.size)
        if cells != model.viewCube { model.viewCube = cells }
        let placed = model.glyphs.compactMap { glyph in
            renderer.project(glyph.at).map { PlacedGlyph(text: glyph.text, point: $0) }
        }
        if placed != model.placedGlyphs { model.placedGlyphs = placed }
        recordFrame(renderer)
    }

    private func recordFrame(_ renderer: ViewportRenderer) {
        let now = CACurrentMediaTime()
        if lastFrameTime > 0 { frameTimes.append(now - lastFrameTime) }
        lastFrameTime = now
        let total = frameTimes.reduce(0, +)
        guard total >= 1 else { return }
        model.frameMilliseconds = total / Double(frameTimes.count) * 1000
        model.drawMilliseconds = Double(renderer.stats.cpu_milliseconds)
        frameTimes.removeAll(keepingCapacity: true)
        viewportLogger.debug(
            "frame \(self.model.frameMilliseconds, format: .fixed(precision: 2)) ms, draw \(self.model.drawMilliseconds, format: .fixed(precision: 3)) ms, \(self.model.triangles) triangles"
        )
    }

    // MARK: - Input, shared by both platforms

    /// A pointer event for the sketch tool at `point` on screen; `radius` is the pointer's pick
    /// radius in points. False if no tool is drawing.
    @discardableResult
    func toolEvent(_ event: ToolEvent, at point: CGPoint, radius: Double) -> Bool {
        guard let drawing = model.drawing else { return false }
        model.pointerLabelAt = point
        if let renderer, let spot = renderer.sketchPoint(at: point, on: drawing.plane) {
            drawing.onPointer(
                event, spot.u, spot.v, radius * spot.perPoint, renderer.gridSpacing)
        }
        return true
    }

    var isDrawing: Bool { model.drawing != nil }

    func hover(at point: CGPoint, radius: Double) {
        guard let renderer else { return }
        renderer.setHover(renderer.pick(at: point, radius: radius))
    }

    /// A click or tap: selects the first candidate at `point`, or the next one when the same
    /// spot is clicked again; `toggle` adds or removes it instead.
    func select(at point: CGPoint, radius: Double, toggle: Bool) {
        guard let renderer else { return }
        let (pick, selected) = renderer.click(
            at: point, radius: radius, toggle: toggle || model.multiSelect || model.regions != nil)
        model.selected = selected
        // The element just added, for repairs; nothing when it was just removed.
        model.selection = selected.contains(pick) ? pick : .none
        shownSelection = model.selection
    }

    /// Shows the precision selector's crosshair at `point` and highlights what it is over, or
    /// hides it (nil).
    func updateCrosshair(_ point: CGPoint?) {
        model.crosshair = point
        guard let renderer else { return }
        renderer.setHover(point.map { renderer.pick(at: $0, radius: PickRadius.pencil) } ?? .none)
    }

    /// Whether there is geometry under `point` (a long press there starts the precision
    /// selector rather than a box).
    func hasGeometry(at point: CGPoint) -> Bool {
        renderer?.pick(at: point, radius: PickRadius.finger).kind != Pick.Kind.none
    }

    /// Shows the box being dragged, or hides it (nil).
    func updateBox(from start: CGPoint?, to end: CGPoint = .zero) {
        model.selectionBox = start.map { ($0, end) }
    }

    /// Selects what the box from `start` to `end` holds and hides the box.
    func finishBox(from start: CGPoint, to end: CGPoint, add: Bool) {
        model.selectionBox = nil
        guard let renderer, hypot(end.x - start.x, end.y - start.y) > 4 else { return }
        model.selected = renderer.boxSelect(
            from: start, to: end, add: add || model.multiSelect || model.regions != nil)
        model.selection = .none
        shownSelection = .none
    }

    fileprivate func select(_ pick: Pick, toggle: Bool) {
        guard let renderer else { return }
        let selected = renderer.select(pick, toggle: toggle)
        model.selected = selected
        // The element just added, for repairs; nothing when it was just removed.
        model.selection = selected.contains(pick) ? pick : .none
        shownSelection = model.selection
    }
}

/// The selection box being dragged: a window (to the right) solid blue, a crossing box (to the
/// left) dashed green, like Fusion.
struct SelectionBoxOverlay: View {
    let model: ViewportModel

    var body: some View {
        Canvas { context, _ in
            guard let (start, end) = model.selectionBox else { return }
            let rect = CGRect(
                x: min(start.x, end.x), y: min(start.y, end.y), width: abs(end.x - start.x),
                height: abs(end.y - start.y))
            let crossing = end.x < start.x
            let color = crossing ? Color(red: 0.19, green: 0.63, blue: 0.31) : Color.accentColor
            context.fill(Path(rect), with: .color(color.opacity(0.1)))
            context.stroke(
                Path(rect), with: .color(color),
                style: StrokeStyle(lineWidth: 1, dash: crossing ? [5, 3] : []))
        }
        .overlay {
            // The precision selector: a crosshair above the finger.
            Canvas { context, _ in
                guard let point = model.crosshair else { return }
                let ring = Path(
                    ellipseIn: CGRect(x: point.x - 14, y: point.y - 14, width: 28, height: 28))
                var cross = Path()
                cross.move(to: CGPoint(x: point.x - 22, y: point.y))
                cross.addLine(to: CGPoint(x: point.x + 22, y: point.y))
                cross.move(to: CGPoint(x: point.x, y: point.y - 22))
                cross.addLine(to: CGPoint(x: point.x, y: point.y + 22))
                context.stroke(ring, with: .color(.white.opacity(0.8)), lineWidth: 4)
                context.stroke(ring, with: .color(.accentColor), lineWidth: 2)
                context.stroke(cross, with: .color(.accentColor), lineWidth: 1)
            }
        }
        .allowsHitTesting(false)
    }
}

#if os(macOS)
    /// Mouse and trackpad: buttons and scrolling follow the navigation preset, pinch zooms toward
    /// the pointer, smart zoom fits, clicks select, the pointer highlights what it is over.
    final class ViewportMTKView: MTKView {
        weak var coordinator: ViewportCoordinator?
        private var dragStart: CGPoint?
        private var lastPoint: CGPoint = .zero
        private var dragAction = NavigationPreset.Action.none
        private var lastScroll: TimeInterval = 0
        /// The left button is down for a sketch tool.
        private var drawingPress = false

        private var navigation: NavigationPreset {
            coordinator?.model.navigation ?? .facetry
        }

        override var isFlipped: Bool { true }  // top-left origin, like the renderer
        override var acceptsFirstResponder: Bool { true }

        override func updateTrackingAreas() {
            super.updateTrackingAreas()
            trackingAreas.forEach(removeTrackingArea)
            addTrackingArea(
                NSTrackingArea(
                    rect: bounds, options: [.mouseMoved, .activeInKeyWindow, .inVisibleRect],
                    owner: self))
        }

        private func location(_ event: NSEvent) -> CGPoint {
            convert(event.locationInWindow, from: nil)
        }

        override func mouseMoved(with event: NSEvent) {
            if coordinator?.toolEvent(.move, at: location(event), radius: PickRadius.mouse) == true
            {
                return  // the tool's preview follows the pointer
            }
            coordinator?.hover(at: location(event), radius: PickRadius.mouse)
        }

        override func mouseDown(with event: NSEvent) {
            // While drawing, the left button draws; with Option it orbits (docs/INPUT.md).
            if coordinator?.isDrawing == true {
                let keys = event.modifierFlags.intersection([.shift, .command, .control, .option])
                if keys.isEmpty {
                    drawingPress = true
                    coordinator?.toolEvent(.down, at: location(event), radius: PickRadius.mouse)
                    return
                }
                if keys == .option {
                    let point = location(event)
                    dragStart = point
                    lastPoint = point
                    dragAction = .orbit
                    coordinator?.renderer?.orbitBegin(at: point)
                    return
                }
            }
            begin(event, button: .left)
            if coordinator?.model.boxMode == true { dragAction = .select }
        }
        override func rightMouseDown(with event: NSEvent) { begin(event, button: .right) }
        override func otherMouseDown(with event: NSEvent) {
            // Button 2 is the middle button; further buttons pan like it.
            begin(event, button: .middle)
        }
        override func mouseDragged(with event: NSEvent) {
            if drawingPress {
                coordinator?.toolEvent(.move, at: location(event), radius: PickRadius.mouse)
                return
            }
            drag(event)
        }
        override func rightMouseDragged(with event: NSEvent) { drag(event) }
        override func otherMouseDragged(with event: NSEvent) { drag(event) }

        override func mouseUp(with event: NSEvent) {
            let point = location(event)
            if drawingPress {
                drawingPress = false
                coordinator?.toolEvent(.up, at: point, radius: PickRadius.mouse)
                return
            }
            let add = !event.modifierFlags.intersection([.shift, .command]).isEmpty
            if dragAction == .select, let start = dragStart,
                hypot(point.x - start.x, point.y - start.y) > 4
            {
                coordinator?.finishBox(from: start, to: point, add: add)
            } else if let start = dragStart, hypot(point.x - start.x, point.y - start.y) <= 4 {
                // Shift or Command add to the selection or remove from it, like in Fusion.
                let toggle = !event.modifierFlags.intersection([.shift, .command]).isEmpty
                coordinator?.select(at: start, radius: PickRadius.mouse, toggle: toggle)
            }
            dragStart = nil
        }
        override func rightMouseUp(with event: NSEvent) { dragStart = nil }
        override func otherMouseUp(with event: NSEvent) { dragStart = nil }

        // Trackpads report precise deltas, mouse wheels line deltas; the preset decides what
        // each does (docs/INPUT.md).
        override func scrollWheel(with event: NSEvent) {
            guard let renderer = coordinator?.renderer else { return }
            let trackpad = event.hasPreciseScrollingDeltas
            // Mouse wheels scroll by lines; about ten points each.
            let scale = trackpad ? 1.0 : 10.0
            let dx = event.scrollingDeltaX * scale
            let dy = event.scrollingDeltaY * scale
            let gap = event.timestamp - lastScroll
            lastScroll = event.timestamp
            switch navigation.scroll(
                modifiers: event.modifierFlags.navigationMask, trackpad: trackpad)
            {
            case .orbit:
                // A trackpad gesture says when it starts; wheel ticks start after a pause.
                if event.phase == .began || (!trackpad && gap > 0.2) {
                    renderer.orbitBegin(at: location(event))
                }
                renderer.orbit(dx: -dx, dy: -dy)
            case .pan:
                renderer.pan(dx: dx, dy: dy)
            case .zoom:
                renderer.zoom(factor: exp(-dy * 0.006), at: location(event))
            case .none, .select:
                break
            }
        }

        override func magnify(with event: NSEvent) {
            coordinator?.renderer?.zoom(factor: 1 / (1 + event.magnification), at: location(event))
        }

        // A two-finger double-tap on the trackpad fits the view, like a two-finger double-tap
        // on the iPad.
        override func smartMagnify(with event: NSEvent) {
            coordinator?.model.fit()
        }

        private func begin(_ event: NSEvent, button: NavigationPreset.Button) {
            let point = location(event)
            dragStart = point
            lastPoint = point
            dragAction = navigation.drag(button, modifiers: event.modifierFlags.navigationMask)
            if dragAction == .orbit { coordinator?.renderer?.orbitBegin(at: point) }
        }

        private func drag(_ event: NSEvent) {
            let point = location(event)
            let dx = point.x - lastPoint.x
            let dy = point.y - lastPoint.y
            lastPoint = point
            switch dragAction {
            case .orbit: coordinator?.renderer?.orbit(dx: dx, dy: dy)
            case .pan: coordinator?.renderer?.pan(dx: dx, dy: dy)
            case .select:
                if let start = dragStart { coordinator?.updateBox(from: start, to: point) }
            case .none, .zoom: break
            }
        }
    }

    struct ViewportView: NSViewRepresentable {
        let model: ViewportModel

        func makeCoordinator() -> ViewportCoordinator { ViewportCoordinator(model: model) }

        func makeNSView(context: Context) -> ViewportMTKView {
            let view = ViewportMTKView()
            view.coordinator = context.coordinator
            context.coordinator.configure(view)
            return view
        }

        func updateNSView(_ view: ViewportMTKView, context: Context) {
            context.coordinator.sync()
        }
    }

#else
    /// Touch and Pencil: one finger (or the Pencil) drags to orbit, two fingers pan and pinch to
    /// zoom (swapped with "one finger pans"), a two-finger double-tap fits, a tap selects, a
    /// hovering Pencil or pointer highlights what it is over. Mouse buttons and scrolling follow
    /// the navigation preset.
    final class ViewportMTKView: MTKView {
        weak var coordinator: ViewportCoordinator?
        private var lastTranslation: CGPoint = .zero
        private var lastPanTranslation: CGPoint = .zero
        private var lastScrollTranslation: CGPoint = .zero
        private var lastScale: CGFloat = 1
        private var oneTouchAction = NavigationPreset.Action.orbit
        private var boxStart: CGPoint?
        /// The precision selector is running (a long press over geometry).
        private var precise = false
        /// The one-touch drag draws with a sketch tool.
        private var drawingTouch = false

        private var navigation: NavigationPreset {
            coordinator?.model.navigation ?? .facetry
        }
        private var oneFingerPans: Bool {
            coordinator?.model.oneFingerPans ?? false
        }

        func installGestures() {
            let orbit = UIPanGestureRecognizer(target: self, action: #selector(handleOrbit))
            orbit.maximumNumberOfTouches = 1
            let pan = UIPanGestureRecognizer(target: self, action: #selector(handlePan))
            pan.minimumNumberOfTouches = 2
            let pinch = UIPinchGestureRecognizer(target: self, action: #selector(handlePinch))
            let tap = UITapGestureRecognizer(target: self, action: #selector(handleTap))
            let fit = UITapGestureRecognizer(target: self, action: #selector(handleFit))
            fit.numberOfTouchesRequired = 2
            fit.numberOfTapsRequired = 2
            let hover = UIHoverGestureRecognizer(target: self, action: #selector(handleHover))
            // Holding a finger still, then dragging, draws a selection box.
            let longPress = UILongPressGestureRecognizer(
                target: self, action: #selector(handleLongPress))
            longPress.minimumPressDuration = 0.45
            // Scrolling without touches: a trackpad scrolls continuously, a mouse wheel in steps.
            let trackpad = UIPanGestureRecognizer(
                target: self, action: #selector(handleTrackpadScroll))
            trackpad.allowedScrollTypesMask = .continuous
            trackpad.allowedTouchTypes = []
            let wheel = UIPanGestureRecognizer(target: self, action: #selector(handleWheelScroll))
            wheel.allowedScrollTypesMask = .discrete
            wheel.allowedTouchTypes = []
            for recognizer in [orbit, pan, pinch, tap, fit, hover, longPress, trackpad, wheel]
                as [UIGestureRecognizer]
            {
                recognizer.delegate = gestureDelegate
                addGestureRecognizer(recognizer)
            }
        }

        private let gestureDelegate = SimultaneousGestures()

        private func radius(for touchType: UITouch.TouchType?) -> Double {
            switch touchType {
            case .pencil: PickRadius.pencil
            case .indirectPointer: PickRadius.mouse
            default: PickRadius.finger
            }
        }

        // One touch: a finger orbits (or pans), the Pencil orbits until drawing tools exist
        // (M1.3), a mouse button follows the preset.
        @objc private func handleOrbit(_ recognizer: UIPanGestureRecognizer) {
            let translation = recognizer.translation(in: self)
            switch recognizer.state {
            case .began:
                lastTranslation = .zero
                // While drawing, the Pencil, a finger and the left mouse button draw: the drag
                // starts where the touch went down (docs/INPUT.md).
                let mouseButton =
                    gestureDelegate.lastTouchType == .indirectPointer
                    && !recognizer.buttonMask.contains(.secondary)
                    && !recognizer.buttonMask.contains(.button(3))
                let keys = recognizer.modifierFlags.intersection([
                    .shift, .command, .control, .alternate,
                ])
                if coordinator?.isDrawing == true,
                    gestureDelegate.lastTouchType != .indirectPointer
                        || (mouseButton && keys.isEmpty)
                {
                    drawingTouch = true
                    let toolRadius = radius(for: gestureDelegate.lastTouchType)
                    let start = CGPoint(
                        x: recognizer.location(in: self).x - translation.x,
                        y: recognizer.location(in: self).y - translation.y)
                    coordinator?.toolEvent(.down, at: start, radius: toolRadius)
                    coordinator?.toolEvent(
                        .move, at: recognizer.location(in: self), radius: toolRadius)
                    return
                }
                drawingTouch = false
                switch gestureDelegate.lastTouchType {
                case .indirectPointer:
                    // The button that started the drag: a mouse's right or middle button too.
                    let button: NavigationPreset.Button =
                        recognizer.buttonMask.contains(.secondary)
                        ? .right : recognizer.buttonMask.contains(.button(3)) ? .middle : .left
                    oneTouchAction = navigation.drag(
                        button, modifiers: recognizer.modifierFlags.navigationMask)
                case .pencil:
                    oneTouchAction = .orbit
                default:
                    oneTouchAction = oneFingerPans ? .pan : .orbit
                }
                // The precision selector owns the finger while it runs.
                if precise {
                    oneTouchAction = .none
                } else if coordinator?.model.boxMode == true || boxStart != nil {
                    // The Box toggle (or a long press already running) makes the drag a box.
                    oneTouchAction = .select
                    boxStart =
                        boxStart
                        ?? CGPoint(
                            x: recognizer.location(in: self).x - translation.x,
                            y: recognizer.location(in: self).y - translation.y)
                }
                if oneTouchAction == .orbit {
                    coordinator?.renderer?.orbitBegin(at: recognizer.location(in: self))
                }
            case .changed:
                if drawingTouch {
                    coordinator?.toolEvent(
                        .move, at: recognizer.location(in: self),
                        radius: radius(for: gestureDelegate.lastTouchType))
                    return
                }
                if oneTouchAction == .select, let boxStart {
                    coordinator?.updateBox(from: boxStart, to: recognizer.location(in: self))
                } else {
                    move(oneTouchAction, by: translation, from: &lastTranslation)
                }
            case .ended:
                if drawingTouch {
                    drawingTouch = false
                    coordinator?.toolEvent(
                        .up, at: recognizer.location(in: self),
                        radius: radius(for: gestureDelegate.lastTouchType))
                    return
                }
                if oneTouchAction == .select, let start = boxStart {
                    coordinator?.finishBox(
                        from: start, to: recognizer.location(in: self),
                        add: !recognizer.modifierFlags.intersection([.shift, .command]).isEmpty)
                }
                boxStart = nil
            default:
                drawingTouch = false
                if oneTouchAction == .select { coordinator?.updateBox(from: nil) }
                boxStart = nil
            }
        }

        // A finger held still: over geometry the precision selector (a crosshair above the
        // finger, selecting where it points on release), over empty space a selection box.
        @objc private func handleLongPress(_ recognizer: UILongPressGestureRecognizer) {
            guard coordinator?.isDrawing != true else { return }  // drawing has no long press
            let location = recognizer.location(in: self)
            let aim = CGPoint(x: location.x, y: location.y - Self.precisionOffset)
            switch recognizer.state {
            case .began:
                if coordinator?.hasGeometry(at: location) == true {
                    precise = true
                    coordinator?.updateCrosshair(aim)
                } else {
                    boxStart = location
                    coordinator?.updateBox(from: boxStart, to: location)
                }
            case .changed:
                if precise {
                    coordinator?.updateCrosshair(aim)
                } else if let boxStart {
                    coordinator?.updateBox(from: boxStart, to: location)
                }
            case .ended:
                if precise {
                    coordinator?.updateCrosshair(nil)
                    coordinator?.select(at: aim, radius: PickRadius.pencil, toggle: false)
                } else if let start = boxStart {
                    // Without a drag the pan recognizer never ran: finish the box here.
                    coordinator?.finishBox(from: start, to: location, add: false)
                }
                boxStart = nil
                precise = false
            default:
                coordinator?.updateBox(from: nil)
                coordinator?.updateCrosshair(nil)
                boxStart = nil
                precise = false
            }
        }

        /// The precision selector's crosshair sits this far above the finger.
        private static let precisionOffset = 60.0

        private func move(
            _ action: NavigationPreset.Action, by translation: CGPoint, from last: inout CGPoint
        ) {
            let dx = translation.x - last.x
            let dy = translation.y - last.y
            last = translation
            switch action {
            case .orbit: coordinator?.renderer?.orbit(dx: dx, dy: dy)
            case .pan: coordinator?.renderer?.pan(dx: dx, dy: dy)
            case .none, .zoom, .select: break  // boxes are drawn by handleOrbit
            }
        }

        @objc private func handleTrackpadScroll(_ recognizer: UIPanGestureRecognizer) {
            scroll(recognizer, trackpad: true)
        }

        @objc private func handleWheelScroll(_ recognizer: UIPanGestureRecognizer) {
            scroll(recognizer, trackpad: false)
        }

        private func scroll(_ recognizer: UIPanGestureRecognizer, trackpad: Bool) {
            let translation = recognizer.translation(in: self)
            let action = navigation.scroll(
                modifiers: recognizer.modifierFlags.navigationMask, trackpad: trackpad)
            switch recognizer.state {
            case .began:
                lastScrollTranslation = .zero
                if action == .orbit {
                    coordinator?.renderer?.orbitBegin(at: recognizer.location(in: self))
                }
            case .changed:
                let dx = translation.x - lastScrollTranslation.x
                let dy = translation.y - lastScrollTranslation.y
                lastScrollTranslation = translation
                switch action {
                case .orbit: coordinator?.renderer?.orbit(dx: -dx, dy: -dy)
                case .pan: coordinator?.renderer?.pan(dx: dx, dy: dy)
                case .zoom:
                    coordinator?.renderer?.zoom(
                        factor: exp(-dy * 0.006), at: recognizer.location(in: self))
                case .none, .select: break
                }
            default:
                break
            }
        }

        // Two fingers pan, or orbit around their midpoint when one finger pans.
        @objc private func handlePan(_ recognizer: UIPanGestureRecognizer) {
            let translation = recognizer.translation(in: self)
            if recognizer.state == .began {
                lastPanTranslation = .zero
                if oneFingerPans {
                    coordinator?.renderer?.orbitBegin(at: recognizer.location(in: self))
                }
            }
            if recognizer.state == .changed {
                move(oneFingerPans ? .orbit : .pan, by: translation, from: &lastPanTranslation)
            }
        }

        @objc private func handleFit(_ recognizer: UITapGestureRecognizer) {
            coordinator?.model.fit()
        }

        @objc private func handlePinch(_ recognizer: UIPinchGestureRecognizer) {
            if recognizer.state == .began { lastScale = 1 }
            if recognizer.state == .changed, recognizer.scale > 0 {
                coordinator?.renderer?.zoom(
                    factor: lastScale / recognizer.scale, at: recognizer.location(in: self))
                lastScale = recognizer.scale
            }
        }

        @objc private func handleTap(_ recognizer: UITapGestureRecognizer) {
            // While drawing, a tap is a click for the tool.
            let pickRadius = radius(for: gestureDelegate.lastTouchType)
            if coordinator?.isDrawing == true {
                let point = recognizer.location(in: self)
                coordinator?.toolEvent(.down, at: point, radius: pickRadius)
                coordinator?.toolEvent(.up, at: point, radius: pickRadius)
                return
            }
            coordinator?.select(
                at: recognizer.location(in: self),
                radius: pickRadius,
                toggle: !recognizer.modifierFlags.intersection([.shift, .command]).isEmpty
            )
        }

        @objc private func handleHover(_ recognizer: UIHoverGestureRecognizer) {
            switch recognizer.state {
            case .began, .changed:
                // Hover comes from a hovering Pencil (zOffset > 0) or a trackpad/mouse pointer.
                let pencil = recognizer.zOffset > 0
                if coordinator?.toolEvent(
                    .move, at: recognizer.location(in: self),
                    radius: pencil ? PickRadius.pencil : PickRadius.mouse) == true
                {
                    return
                }
                coordinator?.hover(
                    at: recognizer.location(in: self),
                    radius: pencil ? PickRadius.pencil : PickRadius.mouse)
            default:
                coordinator?.renderer?.setHover(.none)
            }
        }

        override func layoutSubviews() {
            super.layoutSubviews()
            coordinator?.updateViewport(self)
        }
    }

    /// Lets orbit, pan and pinch run together and remembers whether a finger or the Pencil
    /// touched last (for the pick radius).
    final class SimultaneousGestures: NSObject, UIGestureRecognizerDelegate {
        var lastTouchType: UITouch.TouchType?

        func gestureRecognizer(
            _ gestureRecognizer: UIGestureRecognizer,
            shouldRecognizeSimultaneouslyWith other: UIGestureRecognizer
        ) -> Bool {
            true
        }

        func gestureRecognizer(
            _ gestureRecognizer: UIGestureRecognizer, shouldReceive touch: UITouch
        ) -> Bool {
            lastTouchType = touch.type
            return true
        }
    }

    struct ViewportView: UIViewRepresentable {
        let model: ViewportModel

        func makeCoordinator() -> ViewportCoordinator { ViewportCoordinator(model: model) }

        func makeUIView(context: Context) -> ViewportMTKView {
            let view = ViewportMTKView()
            view.coordinator = context.coordinator
            context.coordinator.configure(view)
            view.installGestures()
            return view
        }

        func updateUIView(_ view: ViewportMTKView, context: Context) {
            context.coordinator.sync()
        }
    }
#endif
