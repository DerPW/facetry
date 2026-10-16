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
    /// The mesh to show. Setting a new mesh frames it.
    var mesh: MeshData?
    var orthographic = false
    var selection = Pick.none
    /// Frame statistics, updated about once per second.
    var frameMilliseconds = 0.0
    var drawMilliseconds = 0.0
    var triangles = 0
    var error: String?

    fileprivate var fitRequests = 0

    func fit() { fitRequests += 1 }
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
    private var lastFrameTime: CFTimeInterval = 0
    private var frameTimes: [Double] = []

    init(model: ViewportModel) {
        self.model = model
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
                renderer.fit()
                model.triangles = mesh.triangleCount
            } catch {
                model.error = "\(error)"
            }
            shownMesh = mesh
        }
        if model.orthographic != shownOrthographic {
            renderer.setOrthographic(model.orthographic)
            shownOrthographic = model.orthographic
        }
        if model.fitRequests != shownFitRequests {
            renderer.fit()
            shownFitRequests = model.fitRequests
        }
    }

    func updateViewport(_ view: MTKView) {
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

    func select(at point: CGPoint, radius: Double) {
        guard let renderer else { return }
        let pick = renderer.pick(at: point, radius: radius)
        renderer.setSelection(pick)
        model.selection = pick
    }
}

#if os(macOS)
    /// Mouse and trackpad: left drag orbits, right/middle or Shift+left drag pans, scroll and
    /// pinch zoom toward the pointer, clicks select, the pointer highlights what it is over.
    final class ViewportMTKView: MTKView {
        weak var coordinator: ViewportCoordinator?
        private var dragStart: CGPoint?
        private var lastPoint: CGPoint = .zero
        private var panning = false

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

        override func mouseDown(with event: NSEvent) {
            begin(event, pan: event.modifierFlags.contains(.shift))
        }
        override func rightMouseDown(with event: NSEvent) { begin(event, pan: true) }
        override func otherMouseDown(with event: NSEvent) { begin(event, pan: true) }
        override func mouseDragged(with event: NSEvent) { drag(event) }
        override func rightMouseDragged(with event: NSEvent) { drag(event) }
        override func otherMouseDragged(with event: NSEvent) { drag(event) }

        override func mouseUp(with event: NSEvent) {
            let point = location(event)
            if let start = dragStart, hypot(point.x - start.x, point.y - start.y) <= 4 {
                coordinator?.select(at: start, radius: PickRadius.mouse)
            }
            dragStart = nil
        }
        override func rightMouseUp(with event: NSEvent) { dragStart = nil }
        override func otherMouseUp(with event: NSEvent) { dragStart = nil }

        override func scrollWheel(with event: NSEvent) {
            let delta =
                event.hasPreciseScrollingDeltas ? event.scrollingDeltaY : event.scrollingDeltaY * 16
            coordinator?.renderer?.zoom(factor: exp(-delta * 0.004), at: location(event))
        }

        override func magnify(with event: NSEvent) {
            coordinator?.renderer?.zoom(factor: 1 / (1 + event.magnification), at: location(event))
        }

        private func begin(_ event: NSEvent, pan: Bool) {
            let point = location(event)
            dragStart = point
            lastPoint = point
            panning = pan
            if !pan { coordinator?.renderer?.orbitBegin(at: point) }
        }

        private func drag(_ event: NSEvent) {
            let point = location(event)
            let dx = point.x - lastPoint.x
            let dy = point.y - lastPoint.y
            lastPoint = point
            if panning {
                coordinator?.renderer?.pan(dx: dx, dy: dy)
            } else {
                coordinator?.renderer?.orbit(dx: dx, dy: dy)
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
    /// zoom, a tap selects, a hovering Pencil or trackpad pointer highlights what it is over.
    final class ViewportMTKView: MTKView {
        weak var coordinator: ViewportCoordinator?
        private var lastTranslation: CGPoint = .zero
        private var lastPanTranslation: CGPoint = .zero
        private var lastScale: CGFloat = 1

        func installGestures() {
            let orbit = UIPanGestureRecognizer(target: self, action: #selector(handleOrbit))
            orbit.maximumNumberOfTouches = 1
            let pan = UIPanGestureRecognizer(target: self, action: #selector(handlePan))
            pan.minimumNumberOfTouches = 2
            let pinch = UIPinchGestureRecognizer(target: self, action: #selector(handlePinch))
            let tap = UITapGestureRecognizer(target: self, action: #selector(handleTap))
            let hover = UIHoverGestureRecognizer(target: self, action: #selector(handleHover))
            for recognizer in [orbit, pan, pinch, tap, hover] as [UIGestureRecognizer] {
                recognizer.delegate = gestureDelegate
                addGestureRecognizer(recognizer)
            }
        }

        private let gestureDelegate = SimultaneousGestures()

        private func radius(for touchType: UITouch.TouchType?) -> Double {
            touchType == .pencil ? PickRadius.pencil : PickRadius.finger
        }

        @objc private func handleOrbit(_ recognizer: UIPanGestureRecognizer) {
            let translation = recognizer.translation(in: self)
            switch recognizer.state {
            case .began:
                lastTranslation = .zero
                coordinator?.renderer?.orbitBegin(at: recognizer.location(in: self))
            case .changed:
                coordinator?.renderer?.orbit(
                    dx: translation.x - lastTranslation.x, dy: translation.y - lastTranslation.y)
                lastTranslation = translation
            default:
                break
            }
        }

        @objc private func handlePan(_ recognizer: UIPanGestureRecognizer) {
            let translation = recognizer.translation(in: self)
            if recognizer.state == .began { lastPanTranslation = .zero }
            if recognizer.state == .changed {
                coordinator?.renderer?.pan(
                    dx: translation.x - lastPanTranslation.x,
                    dy: translation.y - lastPanTranslation.y)
                lastPanTranslation = translation
            }
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
                radius: radius(for: gestureDelegate.lastTouchType)
            )
        }

        @objc private func handleHover(_ recognizer: UIHoverGestureRecognizer) {
            switch recognizer.state {
            case .began, .changed:
                coordinator?.hover(at: recognizer.location(in: self), radius: PickRadius.pencil)
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
