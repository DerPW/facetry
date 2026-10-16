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

---

## M1.1 — Document, timeline & undo

**Size:** L · **Depends on:** M0.6

- [ ] Document structure: root component with origin (XY/XZ/YZ planes, X/Y/Z axes, origin
      point), bodies, sketches, construction geometry
- [ ] **Browser** panel (tree): show/hide, rename, select, isolate, delete
- [ ] **Timeline** bar, Fusion-style:
  - [ ] one icon per feature, in order
  - [ ] drag the rollback marker to view and edit the model at an earlier state
  - [ ] edit a feature (reopens its tool with the stored values)
  - [ ] suppress / unsuppress, rename, delete with a dependency warning
  - [ ] error and warning badges with an explanation
- [ ] Regeneration engine (from the M0.6 prototype):
  - [ ] incremental: regenerate from the first changed feature
  - [ ] runs off the UI thread (background thread on Apple, Web Worker on web)
  - [ ] progress and cancel for long operations
  - [ ] a failed feature does not block independent later features
- [ ] Command system with unlimited undo/redo per session; a tool session is one undo step
- [ ] Native file format v1 (`.facetry`, ADR-0008): versioned schema, migration hook, optional
      geometry cache, embedded thumbnail
- [ ] Autosave and crash recovery
- [ ] Document units (mm, cm, m, in); internal unit is mm

**Done when:** edits to benchmark models survive save → reload → undo → redo on every platform.

## M1.2 — Viewport, navigation & selection

**Size:** M · **Depends on:** M0.4, M1.1

- [ ] Touch gestures: one finger orbits, two fingers pan, pinch zooms, two-finger double-tap fits
      the view (all configurable)
- [ ] Mouse and trackpad: navigation presets (Fusion-like: middle drag pans, Shift + middle drag
      orbits, wheel zooms; plus a trackpad-friendly preset)
- [ ] ViewCube: click faces, edges and corners to change the view; drag to orbit
- [ ] Named views (top, front, right, iso, …), fit all, fit selection, perspective/orthographic
      toggle
- [ ] Visual styles: shaded, shaded with edges, shaded with hidden edges, wireframe;
      transparency per body
- [ ] Ground grid with adaptive spacing
- [ ] Selection:
  - [ ] faces, edges, vertices, bodies, sketch entities
  - [ ] selection filters
  - [ ] multi-select (Shift/Cmd on Mac, toggle mode on iPad)
  - [ ] window and crossing box selection
  - [ ] cycling through overlapping candidates; magnifier for precise touch picks
- [ ] Hover pre-highlight with mouse and with Pencil hover

**Done when:** any edge of the benchmark models can be selected with a finger, with the Pencil
and with a mouse on the first or second attempt.

## M1.3 — Sketching: geometry, snapping & inference

**Size:** L · **Depends on:** M1.2

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

- [ ] Solver integration (PlaneGCS, ADR-0005): solve on every edit; interactive drag solving
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
      targets recorded in `docs/benchmarks.md`
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
