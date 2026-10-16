# Platforms & distribution

Facetry targets three platforms with one core and one file format.

| | iPadOS | macOS | Web |
|---|---|---|---|
| **Role** | Primary touch and Pencil experience | Desktop with keyboard, mouse, trackpad | Try it and share it, no install |
| **Distribution** | App Store (TestFlight for betas) | Signed and notarized download on GitHub Releases | GitHub Pages (installable PWA) |
| **Technology** | SwiftUI + Metal, native core | SwiftUI + Metal, native core | TypeScript, core as WebAssembly in a Web Worker |
| **Minimum version** ([ADR-0009](adr/0009-minimum-os-and-browser-versions.md)) | iPadOS 26 (iPads from 2018 on, A12 or newer) | macOS 26 | Supported: current and previous major of Safari, Chrome, Firefox, Edge. Floor: Chrome/Edge 111, Firefox 114, Safari 16.4 |
| **Architectures** | arm64 | Apple silicon (arm64) | wasm32 |

## Principle for platform differences

Every platform opens and edits the same documents. Features are restricted on a platform only
when it **cannot** do something well, never for other reasons. When a feature is native-only, its
milestone says so and the web app explains why the feature is missing.

## Feature matrix

✅ supported · ◐ partial / depends on browser or device · — not applicable

| Feature | iPadOS | macOS | Web |
|---|---|---|---|
| Full modeling feature set | ✅ | ✅ | ✅ |
| Viewport rendering | ✅ Metal, 120 Hz on ProMotion | ✅ Metal | ✅ WebGL2 (WebGPU later); Safari caps at 60 Hz |
| Apple Pencil drawing and selection | ✅ | — | ◐ via Pointer Events (pressure, tilt) |
| Pencil hover preview | ✅ supported iPads | — | ◐ where the browser reports hover |
| Pencil double-tap, squeeze, barrel roll | ✅ (squeeze and barrel roll need Apple Pencil Pro) | — | — not exposed to the web |
| Scribble handwriting in fields | ✅ | — | ◐ |
| Keyboard shortcuts ([input model](INPUT.md)) | ✅ with hardware keyboard | ✅ | ✅ |
| Multithreaded regeneration | ✅ | ✅ | ◐ needs cross-origin isolation |
| Large models | ✅ limited by device memory | ✅ | ◐ wasm32 memory limit, stricter in Safari on iPad |
| Files / iCloud Drive | ✅ | ✅ | ◐ File System Access API in Chromium; upload/download elsewhere |
| Offline use | ✅ | ✅ | ✅ after the first visit (PWA) |
| STEP import/export | ✅ | ✅ | ✅ |
| STL / 3MF export | ✅ | ✅ | ✅ |
| Fonts for sketch text | ✅ system fonts | ✅ system fonts | ◐ bundled fonts and user-uploaded fonts |
| USDZ export / AR Quick Look | ✅ | ✅ export | ◐ export only |
| Send to slicer | Share sheet | Open with… | Download |
| Blocks editor | ✅ embedded web view | ✅ embedded web view | ✅ |

## Distribution details

### iPadOS: App Store

- Requires the Apple Developer Program (also needed for macOS notarization).
- TestFlight for beta builds, then App Store review.
- Privacy label: **Data Not Collected**. No analytics, no tracking SDKs.
- Open-source licensing: Facetry's own code is MIT. LGPL libraries (OpenCASCADE, PlaneGCS) are
  linked as dynamic frameworks, and their notices are shown in the app. A license review happens
  before the first submission (M1.10).

### macOS: direct download

- Developer ID signing and notarization in CI, then a release on GitHub Releases (DMG or zip).
- Updates: manual at first; an in-app updater (e.g. Sparkle) can follow later.
- A Mac App Store release is possible later from the same project but is not planned for now.

### Web: GitHub Pages

- Deployed automatically from `main` by the CI workflow, so the web app at
  <https://derpw.github.io/facetry/> always shows the latest merged state.
- Installable as a PWA (`manifest.webmanifest`, icons) and usable offline after the first visit:
  a generated service worker precaches every file of the build.
- The repository owner's GitHub Pages use a custom domain, so the URL above redirects there.
- GitHub Pages cannot set custom HTTP headers. WebAssembly threads would need cross-origin
  isolation (COOP/COEP headers). Neither the core nor the renderer uses threads yet, so this is
  not needed. When multithreading comes (M2.9), a service-worker shim can provide the headers;
  if that proves unreliable, the web build moves to a host that allows custom headers.
- The WebAssembly download size is tracked against a budget. M0.2 measured 2.6 MB (brotli) for
  modeling only and 3.3 MB with STEP (see [benchmarks](benchmarks.md)), so STEP is linked in for
  now. Loading it on demand stays an option if the core grows.
- Required browser features: WebAssembly with exceptions and SIMD, WebGL2, module Web Workers;
  OPFS/IndexedDB for autosave (M1.1). The build targets the floor in ADR-0009.
