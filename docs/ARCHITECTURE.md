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
   on the web. The UI talks to it asynchronously. A new edit stops a running regeneration after
   its current feature; the last model stays on screen until a newer one is ready.
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
  history, not indices ([ADR-0006](adr/0006-persistent-naming.md)).
- **Undo/redo** is command-based on the document description. Geometry is a cache that can
  always be rebuilt.

The M0.6 prototype implements this in the core:

| Part | Where | Prototype scope |
|---|---|---|
| Document, components, features, parameters | `core/include/facetry/core/document.hpp` | Root component only; sketch (points, lines, arcs, circles and constraints on an origin plane or a planar face, [ADR-0013](adr/0013-sketch-model.md)), extrude (new body from chosen profiles, [ADR-0014](adr/0014-sketch-profiles.md)), fillet; parameters are numbers that features use by name; lengths in the document's units ([ADR-0012](adr/0012-document-units.md)) |
| Bodies | results in `regeneration.hpp` | A body's ID is the ID of the feature that created it |
| JSON form | `document.hpp` (`toJson`, `parseDocument`) | Versioned (`formatVersion`, per-feature `namingVersion`); examples in `tests/documents/` |
| Naming | `naming.hpp`, `core/src/kernel/occt_naming.cpp` | Faces from history and roles, edges and vertices by their faces; sketch regions by the curves around them and the side ([ADR-0014](adr/0014-sketch-profiles.md)) |
| Regeneration engine | `regeneration.hpp`, `cancellation.hpp` | Per-feature cache keyed by the feature's definition with parameter values filled in; cancellation between features and inside kernel operations that support it (OCCT Booleans and meshing; fillets run to the end); failed features pass the model through |
| Session | `session.hpp` | Undo/redo of named steps; transactions make one step of a whole tool (M1.1). A step keeps the document before it, which is cheap at a few kilobytes |
| C API | `facetry_core.h`, `session_json.hpp` | Sessions with JSON edits and summaries ([ADR-0011](adr/0011-document-api-json-over-c.md)); the apps open the M0.6 example through it |

Feature and sketch element IDs come from one counter in the document and are never reused, so a
reference to something deleted cannot attach to something new.

### Native file format

`.facetry` (proposal, ADR-0008): a zip container with

- `manifest.json`: format version, app version
- `document.json`: the full document description (units, parameters, timeline, references)
- `cache/`: optional geometry cache for fast opening; can always be rebuilt
- `thumbnail.png`

The format is versioned from the first release. Each version bump comes with a migration.

### Autosave and crash recovery

- **Apple:** the document architecture (DocumentGroup) saves the file itself. After every
  change the app hands it the new bytes. iPadOS writes them about 15 s after an edit (measured
  in the simulator) and right away when the app goes to the background. macOS autosaves in
  place as well; its delay is not measured yet.
- **Web:** the core worker writes the open document, in the native format, to the origin
  private file system (IndexedDB where a worker cannot write it) about a second after a change
  and when the tab is hidden (`apps/web/src/core/autosave.ts`). Each tab holds a Web Lock while
  it is open. On start, autosaves whose tab no longer holds its lock are offered for recovery.
  Saving the file removes the tab's autosave.

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
apps/web/         Vite + React + TypeScript app (PWA); src/core/ wraps the C API and runs it in a
                  Web Worker, src/render/ hosts the renderer, src/components/ the React UI
tools/cli/        facetry-cli: build models from JSON, print stats, export STL, check goldens
tools/icons/      render-icons.swift: draws the app icon for both apps
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
| PlaneGCS (from [FreeCAD](https://github.com/FreeCAD/FreeCAD) 1.1.4) | 2D sketch constraint solver (M1.4, [ADR-0005](adr/0005-sketch-solver-planegcs.md)) | LGPL-2.1-or-later |
| [Eigen](https://eigen.tuxfamily.org/) 3.4 | Linear algebra for PlaneGCS (M1.4) | MPL-2.0 |
| [nlohmann/json](https://github.com/nlohmann/json) 3.12 | Document and model JSON | MIT |
| [miniz](https://github.com/richgel999/miniz) | Zip container of the file format (M1.1, [ADR-0008](adr/0008-native-file-format.md)) | MIT |
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
| [0005](adr/0005-sketch-solver-planegcs.md) | Sketch constraint solver: PlaneGCS, vendored C++ | Accepted | M0.7 |
| [0006](adr/0006-persistent-naming.md) | Persistent naming: names from feature history, edges by neighbors, geometric fallback with a warning | Accepted | M0.6 |
| [0007](adr/0007-web-ui-framework-react.md) | Web UI framework: React | Accepted | M0.5 |
| [0008](adr/0008-native-file-format.md) | Native file format: a zip container with manifest, document, thumbnail and cache | Accepted | M0.7 |
| [0009](adr/0009-minimum-os-and-browser-versions.md) | Minimum versions: iPadOS/macOS 26, a feature-based browser floor | Accepted | M0.7 |
| 0010 | Script engine: embedded QuickJS (proposed) vs. platform JS engines | Open | M3.5 |
| [0011](adr/0011-document-api-json-over-c.md) | Document API: sessions with JSON edits over the C API | Accepted | M1.1 |
| [0012](adr/0012-document-units.md) | Document units: values in the document's unit, geometry in millimeters | Accepted | M1.1 |
| [0013](adr/0013-sketch-model.md) | Sketch model: entities on shared points, a plane by name or face reference | Accepted | M1.3 |
| [0014](adr/0014-sketch-profiles.md) | Sketch profiles: regions from splitting the sketch plane, named by the curves around them | Accepted | M1.3 |
| [0015](adr/0015-sketch-tools.md) | Sketch tools: state machines in the core, driven by pointer events on the sketch plane | Accepted | M1.3 |
| [0016](adr/0016-sketch-snapping.md) | Sketch snapping and inference: ranked snaps that leave constraints behind | Accepted | M1.3 |
