# Architecture

This document describes how Facetry is structured and which technical decisions are made or
still open. It is a living document: it changes with the code, through pull requests.

See also: [Roadmap](ROADMAP.md) · [Platforms & distribution](PLATFORMS.md)

## Overview

One shared C++ core holds everything that is not platform UI. Two thin shells, SwiftUI for
iPadOS/macOS and TypeScript for the web, render panels and forward input.

```
┌──────────────────────────────┐   ┌──────────────────────────────┐
│ Apple shell (SwiftUI)        │   │ Web shell (TypeScript)       │
│ iPadOS + macOS               │   │ PWA on GitHub Pages          │
│ panels, menus, Pencil input  │   │ panels, menus, Pointer Events│
└──────────────┬───────────────┘   └──────────────┬───────────────┘
               │   core API: facetry_core.h, C ABI (ADR-0003)      │
┌──────────────┴───────────────────────────────────┴───────────────┐
│ facetry-core (C++20) — native on Apple, WebAssembly in a Worker  │
│                                                                  │
│  interaction   tools as state machines, snapping, inference,     │
│                selection logic, manipulators                     │
│  document      components, bodies, sketches, features, timeline, │
│                parameters, persistent references                 │
│  regeneration  dependency tracking, caching, cancellation        │
│  commands      undo/redo, transactions                           │
│  sketch        constraint solver bridge (PlaneGCS)               │
│  kernel        OpenCASCADE adapter (the only place OCCT is used) │
│  scene         tessellation, edge/vertex buffers, picking IDs    │
│  io            .facetry, STEP, STL, 3MF, …                       │
└──────────────────────────────────────────────────────────────────┘

┌──────────────────────────────────────────────────────────────────┐
│ facetry-render (C++20, ADR-0004) — facetry_render.h               │
│  camera, CPU picking (BVH), highlights, shaders via sokol_gfx    │
│  Metal on Apple (MTKView), WebGL2 on the web (UI thread)         │
│  no OpenCASCADE: renders the core's mesh buffers                 │
└──────────────────────────────────────────────────────────────────┘
```

### Rules

1. **Logic lives in the core.** If a behavior should be the same on iPad and web, such as how a
   tool reacts to input or how snapping works, it belongs in the core, not in a shell.
2. **OCCT stays inside the kernel adapter.** No OpenCASCADE type crosses the adapter boundary.
   This keeps the rest of the code testable and makes a kernel change possible in theory.
3. **The core never blocks the UI.** It runs on a background thread on Apple and in a Web Worker
   on the web. The UI talks to it asynchronously.
4. **The core is deterministic.** The same document regenerates to the same result on every
   platform. This is what makes golden-model tests possible.
5. **No exceptions across the API boundary.** Every call returns a status and a structured
   error.

## Document model

```
Document
├── settings (units, tolerance, …)
├── parameters (user parameters, expressions)
└── Component (root)
    ├── origin (planes, axes, point)
    ├── timeline: [Feature]
    ├── bodies (results of features)
    ├── sketches
    ├── construction geometry
    └── Component (children, from M2.6)
```

- A **feature** has a type, parameters (expressions), input references, a suppressed flag and
  an optional error. Executing it produces shapes and a naming map.
- **Regeneration** runs the timeline in order up to the rollback marker. Results are cached per
  feature; a change invalidates only the features after it.
- **References** to faces, edges and vertices are persistent selectors derived from feature
  history, not indices (see ADR-0006 and roadmap milestone M0.6).
- **Undo/redo** is command-based on the document description. Geometry is a cache that can
  always be rebuilt.

### Native file format

`.facetry` (proposal, ADR-0008): a zip container with

- `manifest.json`: format version, app version, units
- `document.json`: the full document description (timeline, parameters, references)
- `cache/`: optional geometry cache for fast opening; can always be rebuilt
- `thumbnail.png`

The format is versioned from the first release. Each version bump comes with a migration.

## Repository layout

Set up in M0.1. How to build each part: [Building](BUILDING.md).

```
CMakeLists.txt    root CMake project; CMakePresets.json defines all core targets
cmake/            shared CMake helpers (warnings, …)
core/include/     facetry_core.h (the C API for the shells) and facetry/core/*.hpp (C++ API)
core/src/         facetry-core (C++20); src/kernel/ is the only place that uses OpenCASCADE
core/src/script/  JSON model descriptions (CLI and golden tests only)
core/wasm/        the WebAssembly module for the web app
core/tests/       unit tests and golden-model tests
render/           facetry-render: viewport renderer (C API facetry_render.h), shaders in
                  render/shaders/ compiled into render/src/shaders/ by generate.sh
apps/apple/       SwiftUI app for iPadOS and macOS (XcodeGen spec: project.yml);
                  Facetry/Core/ wraps the C API, scripts/build-core.sh builds its frameworks
apps/web/         Vite + TypeScript app; src/core/ wraps the C API and runs it in a Web Worker
tools/cli/        facetry-cli: build models from JSON, print stats, export STL, check goldens
tests/models/     benchmark models (.facetry) with expected results
third_party/      build scripts and pins for OpenCASCADE, PlaneGCS, …
docs/             roadmap, architecture, ADRs, benchmarks
.github/          CI workflow, issue and pull request templates
```

## Testing

- **Unit tests** for the core (doctest or Catch2), run natively and as WebAssembly in Node.
- **Golden-model tests:** every benchmark model is regenerated in CI. Volume, surface area,
  bounding box and topology counts are compared against stored values within tolerance.
- **Edit tests:** scripted parameter changes on benchmark models check that references survive
  (persistent naming).
- **Fuzzing** (from M2.9): random edits on benchmark models must never crash the core.
- **UI tests** stay light: smoke tests per shell; most behavior is tested in the core.

## Third-party components

| Component | Purpose | License |
|---|---|---|
| [OpenCASCADE](https://dev.opencascade.org/) 8.0.1 | B-Rep geometry kernel (prebuilt, see [`third_party/occt`](../third_party/occt/README.md)) | LGPL-2.1 with OCCT exception |
| PlaneGCS (from [FreeCAD](https://github.com/FreeCAD/FreeCAD)) | 2D sketch constraint solver (proposed) | LGPL-2.1-or-later |
| [sokol_gfx](https://github.com/floooh/sokol) | Graphics abstraction for the viewport (Metal, WebGL2) | zlib |
| [Blockly](https://github.com/google/blockly) | Block editor (M3.6) | Apache-2.0 |
| [QuickJS](https://bellard.org/quickjs/) | Script engine (proposed, M3.5) | MIT |

LGPL components are linked dynamically on Apple platforms. Since all of Facetry is open source
with reproducible build scripts, every user can rebuild it against modified libraries. All
notices are collected in `THIRD_PARTY_NOTICES.md` and shown in the app.

## Decisions

Decisions are recorded as ADRs (Architecture Decision Records) in [`docs/adr/`](adr/README.md).

| ADR | Topic | Status | Decided in |
|---|---|---|---|
| [0001](adr/0001-geometry-kernel-opencascade.md) | Geometry kernel: OpenCASCADE | Accepted | M0.1 |
| [0002](adr/0002-shared-core-with-native-and-web-shells.md) | Shared C++ core with native (SwiftUI) and web (TypeScript) shells | Accepted | M0.1 |
| [0003](adr/0003-core-api-boundary-c-abi.md) | Core API boundary: a C ABI with handles | Accepted | M0.3 |
| [0004](adr/0004-viewport-renderer-shared-cpp-sokol.md) | Viewport renderer: one shared C++ renderer on sokol_gfx, CPU picking | Accepted | M0.4 |
| 0005 | Sketch constraint solver: PlaneGCS (proposed) | Open | M0.7 |
| 0006 | Persistent naming scheme | Open | M0.6 |
| 0007 | Web UI framework | Open | M0.5 |
| 0008 | Native file format | Open | M0.7 |
| 0009 | Minimum OS and browser versions | Open | M0.7 |
| 0010 | Script engine: embedded QuickJS (proposed) vs. platform JS engines | Open | M3.5 |
