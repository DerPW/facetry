# Phase 3 — Assemblies & Output

**Goal:** multi-part designs with motion, production deliverables (drawings, flat patterns),
scripting, and a block editor that opens CAD to beginners.

**Ends with:** **v1.0** (intermediate releases v0.6 – v0.9 as milestones land).

**Exit criteria:**
- The Phase 3 [benchmark models](../ROADMAP.md#benchmark-models) can be built on every platform,
  and their regression tests pass.
- The file format is declared stable (see M3.7).

**Order:** the milestones are mostly independent. **M3.5 (Scripting API) and M3.6 (Blocks) can
be pulled forward** to any point after M2.2, since that is when primitives, booleans,
transforms, fillets and patterns all exist.

Back to the [roadmap overview](../ROADMAP.md).

---

## M3.1 — Assemblies & joints

**Size:** XL · **Depends on:** M2.6

- [ ] Ground components; rigid groups; capture position
- [ ] Joints: rigid, revolute, slider, cylindrical, pin-slot, planar, ball
- [ ] Joint origins: snap points on geometry (face centers, edge midpoints, circle centers)
- [ ] Joint offsets and limits
- [ ] As-built joints (join components where they already are)
- [ ] Drive joints and animate motion; motion links (e.g. gear ratios)
- [ ] Interference check during motion (basic)
- [ ] Bill of materials: components, counts, materials, mass; CSV export
- [ ] Stretch: insert other `.facetry` documents as linked components

**Done when:** the hinge benchmark can be built and animated.

## M3.2 — 2D drawings

**Size:** XL · **Depends on:** M2.6

- [ ] Drawing documents generated from 3D: base, projected, section, detail and isometric views
      (hidden-line removal in the core)
- [ ] Annotations: linear, angular, radial and diameter dimensions, center marks, centerlines,
      notes
- [ ] Sheets, sizes, scale, title block template
- [ ] Associative: views and dimensions update when the model changes
- [ ] Export PDF, SVG and DXF; print

**Done when:** the bracket drawing benchmark exports a correct PDF.

## M3.3 — Surface modeling

**Size:** L · **Depends on:** M2.4

- [ ] Surface extrude, revolve, loft, sweep, patch, ruled surface
- [ ] Offset, trim, extend, stitch (closed surfaces become solids), thicken, boundary fill
- [ ] Continuity options (G1/G2) where the kernel supports them

## M3.4 — Sheet metal (stretch goal)

**Size:** XL · **Depends on:** M2.4

OpenCASCADE has no sheet-metal module, so this is built on top of the kernel and is a large
effort. It only happens if earlier phases leave room.

- [ ] Sheet-metal rules: thickness, bend radius, K-factor, relief
- [ ] Flange, face, bend, unfold/refold
- [ ] Flat pattern with bend lines; DXF export

## M3.5 — Scripting API

**Size:** L · **Depends on:** M2.2

- [ ] Script engine decision (ADR-0010). Proposal: an embedded JavaScript engine (e.g. QuickJS)
      inside the core, so scripts behave the same on every platform and run during regeneration
- [ ] API covering primitives, sketches, features, booleans, transforms, patterns, parameters
      and queries (faces, edges, measurements)
- [ ] **Script feature:** a script is a feature in the timeline. Its parameters appear in the
      customizer (M2.1) and its output bodies can be used by later features.
- [ ] Sandboxed: no network or file access, time and memory limits
- [ ] Script editor with syntax highlighting, autocomplete and errors shown in the editor
- [ ] Sample scripts: gear generator, box joints, honeycomb pattern

## M3.6 — Blocks: visual modeling for beginners

**Size:** L · **Depends on:** M3.5, M2.1

A Scratch-style block editor for people with little CAD or coding experience. Blocks generate a
script for the scripting API (M3.5), so a block program is a normal, editable feature in the
timeline.

- [ ] Block editor based on Blockly. Runs natively in the web app; on Apple in an embedded
      web view, with the same block definitions on every platform.
- [ ] Block categories:
  - [ ] **Shapes:** box, cylinder, sphere, cone, torus, extruded/revolved 2D shapes
  - [ ] **2D:** rectangle, circle, polygon, text
  - [ ] **Move:** translate, rotate, mirror, scale
  - [ ] **Combine:** join, cut, intersect
  - [ ] **Modify:** fillet and chamfer all edges or edges by direction, shell
  - [ ] **Repeat:** linear and circular patterns, loops
  - [ ] **Logic & math:** if/else, comparisons, arithmetic, random with a seed
  - [ ] **Variables** and **custom blocks** (reusable modules)
  - [ ] **Slider block:** turns a value into a customizer slider with a range
- [ ] Live preview: the model updates after each block change
- [ ] "Show code": view the generated script; "Edit as code" turns the block program into a
      script feature
- [ ] "Convert to features": bake the result into regular timeline features so it can be edited
      with the normal tools
- [ ] Touch- and Pencil-friendly block dragging on iPad
- [ ] Beginner templates: storage box, name tag, plant pot, cable clip
- [ ] Block labels translated (with the i18n work in M3.7)

**Done when:** the blocks storage box benchmark can be built only with blocks, and its sliders
work on every platform.

## M3.7 — Toward 1.0

**Size:** L · **Depends on:** M3.1 – M3.6 (M3.4 optional)

- [ ] File format stability: compatibility policy (newer apps always open older files) and a
      migration test suite
- [ ] Localization: English and German
- [ ] Accessibility: VoiceOver for panels and the browser tree, Dynamic Type, reduced motion,
      high contrast
- [ ] Complete user documentation and tutorial models
- [ ] Stability targets based on App Store and TestFlight crash reports only (no third-party
      analytics)
- [ ] Tag **v1.0.0**
