# Phase 1 — Core Modeling (MVP)

**Goal:** model real, simple parts end to end on iPad, Mac and web: sketch with constraints,
extrude, fillet, edit anything in the timeline, export for 3D printing.

**Ends with:** **v0.1**, the first release on all three channels.

**Exit criteria:**
- The Phase 1 [benchmark models](../ROADMAP.md#benchmark-models) can be built with the UI on
  every platform, and their regression tests pass.
- Every tool in this phase works with Pencil + fingers, with keyboard + mouse/trackpad, and in
  the browser.
- A document saved on one platform opens and regenerates the same way on the other two.

Back to the [roadmap overview](../ROADMAP.md).

**Refined in M0.7** with what Phase 0 taught:

- The core's document model, naming and regeneration exist (M0.6) but are not reachable from the
  apps yet. M1.1 adds the C API for them before any UI work.
- Parts of navigation and selection were built in M0.4 and M0.5 and are ticked below.
- The solver (ADR-0005) and the file format (ADR-0008) are decided. M1.4 and M1.1 name the
  concrete steps.
- Sizes are now measured in working sessions instead of weeks
  ([roadmap](../ROADMAP.md#milestone-index)). The relative sizes are unchanged: Phase 0 kept
  their order, with M0.6 as the largest. M1.1 grows with the C API; it was L already.
- Issues are created when a milestone starts. M1.1's are open on GitHub.

---

## M1.1 — Document, timeline & undo

**Size:** L · **Depends on:** M0.6

- [x] **C API for documents** ([ADR-0003](../adr/0003-core-api-boundary-c-abi.md),
      [ADR-0011](../adr/0011-document-api-json-over-c.md)), with Swift and TypeScript wrappers.
      It covers:
  - new, open and save;
  - edits as commands;
  - undo/redo;
  - regeneration with progress and cancel;
  - feature status and messages;
  - bodies as meshes with element names;
  - references from picks.
- [x] Document structure: root component with origin (XY/XZ/YZ planes, X/Y/Z axes, origin
      point), bodies with names and visibility, sketches
      ([#16](https://github.com/DerPW/facetry/issues/16)). Construction geometry joins with M1.7;
      drawing the origin in the viewport comes with it
- [x] **Browser** panel (tree): show/hide, rename, select, isolate; selecting opens the feature
      in the timeline, where it can be deleted ([#15](https://github.com/DerPW/facetry/issues/15))
- [x] **Timeline** bar, Fusion-style ([#14](https://github.com/DerPW/facetry/issues/14)):
  - [x] one icon per feature, in order
  - [x] drag the rollback marker to view and edit the model at an earlier state
  - [x] edit a feature: its values in the feature details for now; reopening its tool comes
        with the tools (M1.3–M1.5)
  - [x] suppress / unsuppress, rename, delete with a dependency warning
  - [x] error and warning badges with an explanation
  - [x] broken references ([ADR-0006](../adr/0006-persistent-naming.md)): list each with its last
        known place, pick a replacement as one undo step
- [x] Regeneration engine (from the M0.6 prototype):
  - [x] incremental: regenerate from the first changed feature (core, M0.6)
  - [x] runs off the UI thread (background actor on Apple, Web Worker on web); a new edit stops
        a running regeneration, and the last model stays on screen meanwhile
        ([#13](https://github.com/DerPW/facetry/issues/13))
  - [x] progress and cancel between features (core, M0.6) and inside long kernel operations
        that support it: OCCT's Boolean operations and meshing do, its fillets run to the end
  - [x] a failed feature does not block independent later features (core, M0.6)
- [x] Command system with unlimited undo/redo per session; a tool session is one undo step
      (transactions); steps have names; Apple's undo manager and the web shortcuts drive it
      ([#12](https://github.com/DerPW/facetry/issues/12))
- [x] Native file format v1 (`.facetry`, [ADR-0008](../adr/0008-native-file-format.md)): a zip
      container written by the core (miniz) with manifest, versioned document, migration hook,
      optional thumbnail; opens the provisional M0.5 files as empty documents. Thumbnails are
      rendered by the apps with M1.9; the optional geometry cache follows when models need it
- [x] Autosave and crash recovery ([#17](https://github.com/DerPW/facetry/issues/17)): the
      document architecture on Apple; on the web, OPFS (IndexedDB fallback) with recovery of
      autosaves from closed tabs
- [x] Document units (mm, cm, m, in); internal unit is mm
      ([#18](https://github.com/DerPW/facetry/issues/18), [ADR-0012](../adr/0012-document-units.md))

**Done when:** edits to benchmark models survive save → reload → undo → redo on every platform.

## M1.2 — Viewport, navigation & selection

**Size:** M · **Depends on:** M0.4, M1.1

- [x] Touch gestures: one finger orbits, two fingers pan, pinch zooms (M0.5)
- [x] Two-finger double-tap fits the view; all gestures configurable
      ([#32](https://github.com/DerPW/facetry/issues/32)): one finger orbits or pans
- [x] Mouse and trackpad: navigation presets (Fusion-like: middle drag pans, Shift + middle drag
      orbits, wheel zooms; plus a trackpad-friendly preset)
      ([#32](https://github.com/DerPW/facetry/issues/32))
- [x] ViewCube: click faces, edges and corners to change the view; drag to orbit
      ([#33](https://github.com/DerPW/facetry/issues/33))
- [x] Fit all and perspective/orthographic toggle (`F`, `O`; M0.5)
- [x] Named views (top, front, right, iso, …), fit selection
      ([#33](https://github.com/DerPW/facetry/issues/33)); Home is the iso view, `F` keeps the
      view direction
- [x] Visual styles: shaded, shaded with edges, shaded with hidden edges, wireframe;
      transparency per body ([#35](https://github.com/DerPW/facetry/issues/35))
- [x] Ground grid with adaptive spacing ([#34](https://github.com/DerPW/facetry/issues/34)):
      1, 2 or 5 × 10ⁿ document units, major lines on powers of ten, red and green axes
- [ ] Selection:
  - [x] faces, edges, vertices (M0.4)
  - [ ] bodies, sketch entities
  - [ ] selection filters
  - [ ] multi-select (Shift/Cmd on Mac, toggle mode on iPad)
  - [ ] window and crossing box selection
  - [ ] cycling through overlapping candidates; magnifier for precise touch picks
- [x] Hover pre-highlight with mouse and with Pencil hover (M0.5; checked on a real iPad in
      [#7](https://github.com/DerPW/facetry/issues/7))

**Done when:** any edge of the benchmark models can be selected with a finger, with the Pencil
and with a mouse on the first or second attempt.

## M1.3 — Sketching: geometry, snapping & inference

**Size:** L · **Depends on:** M1.2

- [ ] Sketch model in the core: points, lines, arcs, circles and constraints with stable IDs that
      sketch-based features reference (extends the M0.6 sketch, ADR-0006)
- [ ] Create a sketch on an origin plane or a planar face; the view turns to face the sketch
- [ ] Drawing tools:
  - [ ] line / polyline (with tangent arc by dragging back from the last point)
  - [ ] rectangle: 2-point, 3-point, center
  - [ ] circle: center + diameter, 2-point, 3-point
  - [ ] arc: 3-point, center-point, tangent
  - [ ] point
  - [ ] construction toggle for any entity
- [ ] Editing tools: trim, extend, sketch fillet, delete, move/copy entities
- [ ] Snapping: endpoints, midpoints, centers, quadrants, intersections, on-curve, origin,
      optional grid snap
- [ ] Inference while drawing: horizontal/vertical, parallel/perpendicular to nearby lines,
      tangent, alignment guides. Inferred constraints are created automatically and shown.
- [ ] Profile detection: closed regions, including nested islands, are highlighted and
      selectable
- [ ] Project origin geometry and face edges into the sketch (basic projection)

**Done when:** all benchmark sketches can be drawn without typing coordinates.

## M1.4 — Sketch constraints & dimensions

**Size:** L · **Depends on:** M1.3

- [ ] Solver integration ([ADR-0005](../adr/0005-sketch-solver-planegcs.md)):
  - [ ] PlaneGCS from FreeCAD 1.1.4 vendored unmodified with shim headers, plus Eigen 3.4;
        notices updated
  - [ ] solve on every edit; interactive drag solving
  - [ ] diagnosis (degrees of freedom, conflicting, redundant) after each edit, off the UI
        thread
- [ ] Geometric constraints: coincident, horizontal, vertical, parallel, perpendicular, tangent,
      equal, concentric, midpoint, collinear, symmetric, fix
- [ ] Dimensions: linear (aligned, horizontal, vertical), radius, diameter, angle
  - [ ] place by dragging, edit by tapping
  - [ ] driving and driven (reference) dimensions
- [ ] Numeric input with expressions and units (`20 mm + 5`, `1/2 in`, `30 deg`)
- [ ] Constraint status like Fusion: under-constrained geometry is blue, fully constrained is
      black; remaining degrees of freedom shown
- [ ] Conflict handling: over-constraining is reported, and the conflicting constraints are
      highlighted instead of being silently ignored
- [ ] Constraint glyphs: show, hide, select, delete
- [ ] Smooth dragging of under-constrained geometry

**Done when:** every benchmark sketch can be fully constrained, and adding a conflicting
constraint shows a clear message.

## M1.5 — Solid features

**Size:** L · **Depends on:** M1.4

- [ ] **Primitives:** box, cylinder, sphere, cone, torus. Parametric, recorded in the timeline.
      Also the entry point for beginners and for the blocks editor later (M3.6).
- [ ] **Extrude:**
  - [ ] input: sketch profiles or planar faces
  - [ ] extents: distance, symmetric, two-sided
  - [ ] operations: new body, join, cut, intersect; cut is suggested automatically when
        extruding into a body
  - [ ] optional taper angle
- [ ] **Revolve:** profile + axis (sketch line, construction axis or origin axis); angle or full
- [ ] **Fillet:** constant radius, multiple edges, tangent-chain propagation, select a face to
      fillet all its edges
- [ ] **Chamfer:** equal distance, two distances, distance + angle
- [ ] **Combine:** join, cut, intersect between bodies; option to keep the tool bodies
- [ ] Manipulators: drag arrows and rings with live preview and a numeric field, snapping to
      sensible steps
- [ ] Errors: e.g. a fillet radius that is too large shows a clear message, marks the feature
      red and leaves the rest of the model usable
- [ ] Every feature names its results through the shared naming pass
      ([ADR-0006](../adr/0006-persistent-naming.md)) and gets edit scenarios like M0.6. For
      booleans, faces keep the names they had in either input.

**Done when:** benchmark models 1–4 can be built.

## M1.6 — Direct manipulation

**Size:** M · **Depends on:** M1.5

- [ ] **Press/Pull** (Fusion semantics, recorded in the timeline):
  - [ ] on a planar face → extrude (join/cut) or offset face
  - [ ] on a fillet face → edits the fillet radius
  - [ ] on an edge → creates a fillet
- [ ] **Move/Rotate** bodies and sketches with a gizmo: translate, rotate, snapping, numeric input;
      recorded as a Move feature
- [ ] **Offset face** for planar and simple faces
- [ ] **Context toolbar:** after a selection, the most relevant actions appear next to it
      (Shapr3D-style). The full toolbar stays available.

**Done when:** a box can be resized only by pushing its faces, and every push can be edited
later in the timeline.

## M1.7 — Construction geometry & inspection

**Size:** M · **Depends on:** M1.5

- [ ] Construction planes: offset, at angle, through three points, midplane, normal to edge at
      point
- [ ] Construction axes: through an edge, through two points, cylinder axis, intersection of two
      planes
- [ ] Construction points: at a vertex, edge midpoint, circle center
- [ ] **Measure:** distance (minimum between any two selections), angle, length, area, radius
- [ ] Live measurement of the current selection in the status bar
- [ ] Body properties: volume, surface area, bounding box, center of mass (mass when a density
      is set)

**Done when:** every dimension of the benchmark models can be checked with the measure tool.

## M1.8 — Pencil, touch & keyboard interaction

**Size:** L · **Depends on:** M1.3, M1.5

The milestone that makes Facetry feel like Facetry. It refines all tools built so far.

- [ ] Input model: Pencil draws and selects precisely, fingers navigate. Optional
      "Pencil only draws" mode. Same model on the web via `pointerType`.
- [ ] Pencil hover: preview snaps and highlights before the tip touches (supported iPads; on the
      web where the browser reports hover)
- [ ] Pencil double-tap and squeeze (Apple Pencil Pro): configurable (undo, toggle construction,
      open the radial menu)
- [ ] Barrel roll (Apple Pencil Pro): rotate the gizmo or angle input (experimental)
- [ ] Radial marking menu (like Fusion's): long-press on touch, right-click with the mouse,
      squeeze with the Pencil; includes "repeat last command"
- [ ] Freehand stroke recognition in sketches: line, arc, circle and rectangle become clean,
      constrained entities; a scribble over an entity deletes it
- [ ] Scribble handwriting in numeric fields (native iPadOS)
- [ ] Keyboard shortcuts close to Fusion: `L` line, `R` rectangle, `C` circle, `D` dimension,
      `T` trim, `X` construction, `P` project, `E` extrude, `F` fillet, `Q` press/pull, `M` move,
      `S` command search, `Esc` cancel. Also with a hardware keyboard on iPad.
- [ ] Command search: fuzzy search over all commands
- [ ] Ergonomics: larger hit targets option, left-handed layout

**Done when:** every Phase 1 tool can be used with only Pencil and fingers, and with only
keyboard and mouse.

## M1.9 — Import, export & files

**Size:** M · **Depends on:** M1.5

- [ ] Export:
  - [ ] STL (binary; per body or merged; tessellation quality setting)
  - [ ] 3MF (multiple bodies, units, basic colors) for slicers
  - [ ] STEP AP242
- [ ] Import STEP: bodies become non-parametric base features that later features can build on
- [ ] Apple: Files app, iCloud Drive, recent documents, document thumbnails, drag and drop
- [ ] Web:
  - [ ] open and save with the File System Access API where available
  - [ ] upload/download fallback in other browsers
  - [ ] autosave to browser storage (OPFS/IndexedDB)
- [ ] "Send to slicer": share sheet on iPad, "Open with" on Mac, download on web

**Done when:** every benchmark model round-trips through STEP and exports a 3MF that slices and
prints correctly.

## M1.10 — MVP release v0.1

**Size:** M · **Depends on:** M1.1 – M1.9

- [ ] All Phase 1 benchmark tests pass on every platform
- [ ] Performance targets met: benchmark models regenerate fast enough to edit interactively;
      targets recorded in `docs/benchmarks.md` (M0.7 baseline: the M0.6 example regenerates in
      2.0 ms natively and 3.6 ms in WebAssembly)
- [ ] Browsers below the floor of [ADR-0009](../adr/0009-minimum-os-and-browser-versions.md)
      get a clear message instead of an empty page
- [ ] Onboarding: sample documents, first-run tips, keyboard shortcut overview
- [ ] Settings: default units, navigation preset, Pencil behavior, light/dark theme
- [ ] i18n infrastructure in place (English only at first, German in M3.7)
- [ ] User guide basics, published with the web app
- [ ] Third-party notices shown in the app; license review done (OCCT, PlaneGCS)
- [ ] Distribution:
  - [ ] web on GitHub Pages
  - [ ] macOS: signed and notarized build on GitHub Releases
  - [ ] iPadOS: TestFlight beta, then App Store submission (privacy label: no data collected)
- [ ] Tag **v0.1.0**
