// Renders the Facetry app icon (a faceted, isometric cube) as PNGs for the web app and the Apple
// app. Run from the repository root on macOS after changing the design:
//
//   swift tools/icons/render-icons.swift
//
// The outputs are checked in.

import CoreGraphics
import Foundation
import ImageIO
import UniformTypeIdentifiers

func color(_ hex: UInt32, _ alpha: CGFloat = 1) -> CGColor {
    CGColor(
        srgbRed: CGFloat((hex >> 16) & 0xFF) / 255, green: CGFloat((hex >> 8) & 0xFF) / 255,
        blue: CGFloat(hex & 0xFF) / 255, alpha: alpha)
}

/// Draws the icon into a square of `size` pixels. `contentScale` shrinks the cube, e.g. for
/// maskable web icons, whose safe zone is the inner 80 %.
func renderIcon(size: Int, contentScale: CGFloat = 1) -> CGImage {
    let s = CGFloat(size)
    let context = CGContext(
        data: nil, width: size, height: size, bitsPerComponent: 8, bytesPerRow: 0,
        space: CGColorSpace(name: CGColorSpace.sRGB)!,
        bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
    // CoreGraphics' origin is bottom-left; flip so the math below reads top-down.
    context.translateBy(x: 0, y: s)
    context.scaleBy(x: 1, y: -1)

    // Background: full-bleed blue gradient (platforms apply their own mask).
    let gradient = CGGradient(
        colorsSpace: CGColorSpace(name: CGColorSpace.sRGB),
        colors: [color(0x3B82F6), color(0x1E3A8A)] as CFArray, locations: [0, 1])!
    context.drawLinearGradient(
        gradient, start: CGPoint(x: 0, y: 0), end: CGPoint(x: s, y: s), options: [])

    // Isometric cube centered in the icon.
    let r = s * 0.30 * contentScale  // distance from center to the outer corners
    let c = CGPoint(x: s / 2, y: s / 2 + s * 0.02 * contentScale)
    let cos30 = cos(CGFloat.pi / 6)
    let top = CGPoint(x: c.x, y: c.y - r)
    let upperRight = CGPoint(x: c.x + r * cos30, y: c.y - r / 2)
    let lowerRight = CGPoint(x: c.x + r * cos30, y: c.y + r / 2)
    let bottom = CGPoint(x: c.x, y: c.y + r)
    let lowerLeft = CGPoint(x: c.x - r * cos30, y: c.y + r / 2)
    let upperLeft = CGPoint(x: c.x - r * cos30, y: c.y - r / 2)

    func face(_ points: [CGPoint], _ fill: CGColor) {
        context.beginPath()
        context.addLines(between: points)
        context.closePath()
        context.setFillColor(fill)
        context.fillPath()
    }
    face([top, upperRight, c, upperLeft], color(0xFFFFFF))  // top
    face([upperLeft, c, bottom, lowerLeft], color(0xDBEAFE))  // left
    face([c, upperRight, lowerRight, bottom], color(0x93C5FD))  // right

    // A chamfer-like facet on the top-right edge, the "facetry" touch.
    let f: CGFloat = 0.34
    let a = CGPoint(x: top.x + (upperRight.x - top.x) * f, y: top.y + (upperRight.y - top.y) * f)
    let b = CGPoint(
        x: upperRight.x + (lowerRight.x - upperRight.x) * f,
        y: upperRight.y + (lowerRight.y - upperRight.y) * f)
    let m = CGPoint(x: upperRight.x - (upperRight.x - c.x) * f, y: upperRight.y - (upperRight.y - c.y) * f)
    face([a, upperRight, b, m], color(0x60A5FA))

    // Crisp edges, like the viewport.
    context.setStrokeColor(color(0x1E3A8A, 0.55))
    context.setLineWidth(max(1, s * 0.008))
    context.setLineJoin(.round)
    for line in [[top, upperRight, lowerRight, bottom, lowerLeft, upperLeft, top], [upperLeft, c, upperRight], [c, bottom]] {
        context.beginPath()
        context.addLines(between: line)
        context.strokePath()
    }
    return context.makeImage()!
}

func writePNG(_ image: CGImage, to path: String) {
    let url = URL(fileURLWithPath: path)
    try? FileManager.default.createDirectory(
        at: url.deletingLastPathComponent(), withIntermediateDirectories: true)
    let destination = CGImageDestinationCreateWithURL(url as CFURL, UTType.png.identifier as CFString, 1, nil)!
    CGImageDestinationAddImage(destination, image, nil)
    guard CGImageDestinationFinalize(destination) else { fatalError("Could not write \(path)") }
    print("Wrote \(path)")
}

// Web app (PWA manifest, favicon, Apple touch icon).
let web = "apps/web/public/icons"
writePNG(renderIcon(size: 192), to: "\(web)/icon-192.png")
writePNG(renderIcon(size: 512), to: "\(web)/icon-512.png")
writePNG(renderIcon(size: 512, contentScale: 0.8), to: "\(web)/icon-maskable-512.png")
writePNG(renderIcon(size: 180), to: "\(web)/apple-touch-icon.png")
writePNG(renderIcon(size: 64), to: "\(web)/favicon-64.png")

// Apple app icon: one 1024 px image for iOS, the usual sizes for macOS.
let appIcon = "apps/apple/Facetry/Assets.xcassets/AppIcon.appiconset"
writePNG(renderIcon(size: 1024), to: "\(appIcon)/icon-1024.png")
for points in [16, 32, 128, 256, 512] {
    writePNG(renderIcon(size: points), to: "\(appIcon)/icon-mac-\(points).png")
    writePNG(renderIcon(size: points * 2), to: "\(appIcon)/icon-mac-\(points)@2x.png")
}
