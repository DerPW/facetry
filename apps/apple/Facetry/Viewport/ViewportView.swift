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
    private var shownPickFilter = PickFilter.everything
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
        if model.pickFilter != shownPickFilter {
            renderer.setPickFilter(model.pickFilter)
            shownPickFilter = model.pickFilter
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
        // The ViewCube follows the camera, including its animations.
        let cells = renderer.viewCubeCells(size: ViewCubeView.size)
        if cells != model.viewCube { model.viewCube = cells }
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

    func hover(at point: CGPoint, radius: Double) {
        guard let renderer else { return }
        renderer.setHover(renderer.pick(at: point, radius: radius))
    }

    /// `toggle`: add or remove the picked element; otherwise select only it (nothing clears).
    func select(at point: CGPoint, radius: Double, toggle: Bool) {
        guard let renderer else { return }
        select(renderer.pick(at: point, radius: radius), toggle: toggle || model.multiSelect)
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

#if os(macOS)
    /// Mouse and trackpad: buttons and scrolling follow the navigation preset, pinch zooms toward
    /// the pointer, smart zoom fits, clicks select, the pointer highlights what it is over.
    final class ViewportMTKView: MTKView {
        weak var coordinator: ViewportCoordinator?
        private var dragStart: CGPoint?
        private var lastPoint: CGPoint = .zero
        private var dragAction = NavigationPreset.Action.none
        private var lastScroll: TimeInterval = 0

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
            coordinator?.hover(at: location(event), radius: PickRadius.mouse)
        }

        override func mouseDown(with event: NSEvent) { begin(event, button: .left) }
        override func rightMouseDown(with event: NSEvent) { begin(event, button: .right) }
        override func otherMouseDown(with event: NSEvent) {
            // Button 2 is the middle button; further buttons pan like it.
            begin(event, button: .middle)
        }
        override func mouseDragged(with event: NSEvent) { drag(event) }
        override func rightMouseDragged(with event: NSEvent) { drag(event) }
        override func otherMouseDragged(with event: NSEvent) { drag(event) }

        override func mouseUp(with event: NSEvent) {
            let point = location(event)
            if let start = dragStart, hypot(point.x - start.x, point.y - start.y) <= 4 {
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
            case .none, .zoom, .select: break  // selection boxes come with #37
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
            // Scrolling without touches: a trackpad scrolls continuously, a mouse wheel in steps.
            let trackpad = UIPanGestureRecognizer(
                target: self, action: #selector(handleTrackpadScroll))
            trackpad.allowedScrollTypesMask = .continuous
            trackpad.allowedTouchTypes = []
            let wheel = UIPanGestureRecognizer(target: self, action: #selector(handleWheelScroll))
            wheel.allowedScrollTypesMask = .discrete
            wheel.allowedTouchTypes = []
            for recognizer in [orbit, pan, pinch, tap, fit, hover, trackpad, wheel]
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
                if oneTouchAction == .orbit {
                    coordinator?.renderer?.orbitBegin(at: recognizer.location(in: self))
                }
            case .changed:
                move(oneTouchAction, by: translation, from: &lastTranslation)
            default:
                break
            }
        }

        private func move(
            _ action: NavigationPreset.Action, by translation: CGPoint, from last: inout CGPoint
        ) {
            let dx = translation.x - last.x
            let dy = translation.y - last.y
            last = translation
            switch action {
            case .orbit: coordinator?.renderer?.orbit(dx: dx, dy: dy)
            case .pan: coordinator?.renderer?.pan(dx: dx, dy: dy)
            case .none, .zoom, .select: break  // selection boxes come with #37
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
            coordinator?.select(
                at: recognizer.location(in: self),
                radius: radius(for: gestureDelegate.lastTouchType),
                toggle: !recognizer.modifierFlags.intersection([.shift, .command]).isEmpty
            )
        }

        @objc private func handleHover(_ recognizer: UIHoverGestureRecognizer) {
            switch recognizer.state {
            case .began, .changed:
                // Hover comes from a hovering Pencil (zOffset > 0) or a trackpad/mouse pointer.
                let pencil = recognizer.zOffset > 0
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
