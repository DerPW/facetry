# Phase 0 — Foundation

**Goal:** prove the whole technical stack end to end on iPad, Mac and web before building any
modeling features.

**Ends with:** v0.0.1 tech preview: a filleted box rendered by the shared core that you can
rotate and select in the browser, on the iPad and on the Mac.

**Exit criteria:**
- The shared C++ core builds and passes its tests in CI for macOS, iPadOS (device and simulator)
  and WebAssembly.
- A box with a filleted edge is created by the core, tessellated, rendered and pickable on all
  three platforms.
- The persistent naming prototype passes all scenarios in M0.6.
- The open architecture decisions listed in [Architecture](../ARCHITECTURE.md#decisions) are
  decided and recorded as ADRs.

Back to the [roadmap overview](../ROADMAP.md).

---

## M0.1 — Repository & engineering setup

**Size:** S · **Depends on:** —

Set up the repository so that every later milestone can be built, tested and reviewed the same way.

- [x] Monorepo layout as described in [Architecture](../ARCHITECTURE.md#repository-layout)
- [x] CMake presets: `macos-debug`, `macos-release`, `ios-device`, `ios-simulator`, `wasm-debug`, `wasm-release`
- [x] Code style and tooling: `clang-format`, `swift-format`, Prettier + ESLint, `.editorconfig`
- [x] GitHub templates: pull request template, issue templates (bug, feature, task)
- [x] Labels: `area:*` (core, sketch, render, apple, web, docs, ci), `platform:*` (ipados, macos, web), `type:*`
- [x] GitHub milestones mirroring this roadmap
- [x] CI with GitHub Actions (macOS runners for Apple targets, Linux for WebAssembly and web):
  - [x] build the core and run its tests (native)
  - [x] build the core for the iOS simulator and device (build only until the app links the core)
  - [x] build the core for WebAssembly and run its tests in Node
  - [x] build the Apple app for the iPadOS simulator and macOS
  - [x] build the web app
- [x] CI checks added as required status checks to the `main` ruleset
- [x] ADR template in `docs/adr/`, plus ADR-0001 (geometry kernel) and ADR-0002 (shared core with native and web shells)
- [x] `THIRD_PARTY_NOTICES.md` started
- [x] Build instructions in [`docs/BUILDING.md`](../BUILDING.md)

**Done when:** a trivial core function is built and tested in CI for all targets, and a PR shows
green checks for each of them.

## M0.2 — OpenCASCADE builds for Apple and WebAssembly

**Size:** M · **Depends on:** M0.1

OpenCASCADE (OCCT) is the geometry kernel (ADR-0001). It is big and slow to build, so it is built
once per version and cached.

- [x] Pin an OCCT release; record the version and the reason in ADR-0001 (8.0.1)
- [x] Build script in `third_party/occt/` that produces:
  - [x] an XCFramework with dynamic frameworks for iOS arm64, iOS simulator arm64 and macOS arm64
  - [x] static libraries for WebAssembly with Emscripten (C++ exceptions enabled, SIMD enabled)
- [x] Minimal toolkit selection: foundation, modeling (primitives, booleans, fillets, offsets,
      local features), meshing, shape healing, STEP. No OpenGL rendering, no Draw harness.
      (STEP pulls in TKService and TKV3d, built without OpenGL.)
- [x] Prebuilt artifacts published as versioned GitHub Release assets and cached in CI, so
      normal CI runs never rebuild OCCT
- [x] Measure and document: binary size per platform, WebAssembly download size (gzip and
      brotli), first-load time in Safari and Chrome, baseline memory
      ([benchmarks](../benchmarks.md))
- [x] License notes for OCCT (LGPL-2.1 with the OCCT exception) in `THIRD_PARTY_NOTICES.md`
- [x] Core tests run inside the iOS simulator (`ctest --preset ios-simulator`)

**Done when:** a C++ test that creates a box, fillets one edge and checks the resulting volume
passes on macOS, in the iPadOS simulator and in Node (WebAssembly).

## M0.3 — Core skeleton & API boundary

**Size:** M · **Depends on:** M0.2

- [x] `facetry-core` C++20 library with a kernel adapter. OCCT types never leave the adapter.
- [x] Decide the API boundary (ADR-0003): a plain C ABI used by Swift and WebAssembly, or Swift
      C++ interop on Apple plus Embind on the web → **C ABI with handles**
      ([ADR-0003](../adr/0003-core-api-boundary-c-abi.md))
- [x] Error model: no exceptions cross the boundary; every call returns a status plus a
      structured error
- [x] Logging with levels, routed to the platform log (OSLog, browser console)
- [x] API versioning, so the shells can detect a mismatched core
- [x] Tessellation output designed for direct GPU upload:
  - [x] triangle buffers (positions, normals) with a face ID per triangle
  - [x] edge polylines with edge IDs
  - [x] vertex positions with vertex IDs
  - [x] tessellation quality settings (linear and angular deflection)
- [x] `facetry-cli`: build a model from a JSON description, print stats, export STL. Used for
      tests and debugging.
- [x] Test framework (doctest or Catch2) and the first golden test: volume, area, bounding box
      and face/edge/vertex counts within tolerance (three models in `tests/models/`)
- [x] Shell wrappers: Swift (`apps/apple/Facetry/Core/`) and TypeScript (`apps/web/src/core/`),
      each tested against the real core
- [x] WebAssembly build runs the core in a Web Worker, never on the main thread

**Done when:** Swift and TypeScript can both call `makeBox → fillet → tessellate` and receive
buffers they can render.

## M0.4 — Viewport renderer spike

**Size:** M · **Depends on:** M0.3

- [x] Decide the renderer approach (ADR-0004): one shared C++ renderer on a thin graphics
      abstraction (Metal and WebGPU/WebGL2), or a native Metal renderer plus a separate web
      renderer → **shared C++ renderer on sokol_gfx**
      ([ADR-0004](../adr/0004-viewport-renderer-shared-cpp-sokol.md))
- [x] Shaded faces with simple, CAD-style lighting (hemisphere light plus a key light)
- [x] Crisp edges with constant screen-space width, and vertex dots
- [x] Camera: perspective and orthographic, orbit around the point under the cursor or finger,
      pan, zoom to the cursor
- [x] Picking for faces, edges and vertices with a configurable pick radius for touch —
      **on the CPU** (BVH ray cast plus screen-space distance) instead of a GPU ID buffer, see
      ADR-0004
- [x] Hover and selection highlight
- [x] MSAA, high-DPI and 120 Hz ProMotion support
- [x] Frame-time overlay for debugging
- [x] Viewport hosted in both apps: `ViewportView` (MTKView) on iPadOS/macOS, a WebGL2 canvas in
      the web app

**Done when:** a 100k-triangle model renders at the display refresh rate on the reference iPad,
on the Mac and in current Safari and Chrome, and edges can be picked reliably with a finger.

**Status:** a 215k-triangle model runs at 120 Hz on the Mac (Metal) and in Chromium, and is far
inside the frame budget in Safari, which caps at 60 Hz (see [benchmarks](../benchmarks.md)).
Finger picking uses an 18-point radius and is covered by unit tests. **Still to verify on a real
iPad** (frame rate and finger picking) once the app shell can be installed on a device (M0.5).

## M0.5 — App shells

**Size:** M · **Depends on:** M0.4

- [x] **Apple:** SwiftUI multiplatform app (iPadOS + macOS)
  - [x] document-based app skeleton (opens and saves an empty `.facetry` document; provisional
        JSON content until ADR-0008)
  - [x] Metal view hosting the viewport (done in M0.4)
  - [x] input routing that tells Pencil, finger, mouse, trackpad and keyboard apart
        ([input model](../INPUT.md))
  - [x] app icon
- [x] **Web:** Vite + TypeScript app (UI framework decided in ADR-0007 → **React**,
      [ADR-0007](../adr/0007-web-ui-framework-react.md))
  - [x] core in a Web Worker, viewport on a canvas (done in M0.3/M0.4)
  - [x] Pointer Events with `pointerType` (pen, touch, mouse) routed the same way as on Apple
  - [x] PWA manifest and service worker for offline use (checked offline in headless Chrome)
- [x] Web build deployed from `main` to GitHub Pages
- [x] Cross-origin isolation for WebAssembly threads on GitHub Pages (service-worker shim) if
      ADR-0004/0003 require threads → **not needed yet**: neither the core nor the renderer uses
      threads. It comes back when the core goes multithreaded (M2.9).
- [x] Checks on a real iPad: frame rate, finger picking, Pencil hover
      ([#7](https://github.com/DerPW/facetry/issues/7); 120 Hz with 215k triangles on an iPad
      Pro M4)

**Done when:** all three platforms show the filleted box, tapping or clicking selects a face,
and hovering highlights it with the mouse and with Pencil hover on supported iPads.

**Status:** done. Mac, web (Chromium, Safari), the iPad simulator and a real iPad (iPad Pro M4,
iPadOS 27) do all of this.

## M0.6 — Document model & persistent naming prototype

**Size:** L · **Depends on:** M0.3

This is the most important milestone of Phase 0. If references to faces and edges are not
stable across edits, parametric modeling does not work. FreeCAD struggled with this for years.

- [x] Data model draft: document, components, bodies, sketches, features, timeline, parameters
      (see [Architecture](../ARCHITECTURE.md#document-model))
- [x] Feature contract: inputs (parameters + references) → execute → outputs (shapes + naming
      map) or an error
- [x] Persistent naming design ([ADR-0006](../adr/0006-persistent-naming.md)):
  - [x] references are stored as topological selectors derived from feature history
        (which input generated or modified which face or edge), not as indices
  - [x] geometric matching as a fallback when history is ambiguous
  - [x] a broken reference is reported on the feature, with a concept for a repair UI
- [x] Regeneration engine prototype: dirty tracking, per-feature result cache, cancellation
- [x] Prototype features: sketch rectangle → extrude → fillet one edge
- [x] Automated scenarios that must keep the fillet on the intended edge:
  - [x] change the rectangle width
  - [x] change the extrude height
  - [x] replace the rectangle with a polygon that has one more edge before the filleted one
  - [x] delete the fillet, undo, regenerate
  - [x] save, reload, regenerate

**Done when:** all scenarios pass in CI on native and WebAssembly builds.

## M0.7 — Phase 0 wrap-up & tech preview

**Size:** S · **Depends on:** M0.1 – M0.6

- [x] Benchmarks recorded in `docs/benchmarks.md`: WebAssembly size, memory, regeneration time,
      frame time per platform
- [x] Remaining ADRs decided: sketch solver
      ([ADR-0005](../adr/0005-sketch-solver-planegcs.md)), web UI framework
      ([ADR-0007](../adr/0007-web-ui-framework-react.md)), native file format
      ([ADR-0008](../adr/0008-native-file-format.md)), minimum OS and browser versions
      ([ADR-0009](../adr/0009-minimum-os-and-browser-versions.md))
- [x] Phase 1 milestones refined from what Phase 0 taught; sizes recalibrated; issues created
- [x] Tag **v0.0.1** and link the web tech preview from the README
