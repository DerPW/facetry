# Phase 2 — Advanced Modeling

**Goal:** cover most parts that makers and product designers build, so that Facetry feels close
to Fusion's solid modeling workspace.

**Ends with:** v0.5 (intermediate releases v0.2 – v0.4, see the
[release plan](../ROADMAP.md#release-plan)).

**Exit criteria:**
- The Phase 2 [benchmark models](../ROADMAP.md#benchmark-models) can be built on every platform,
  and their regression tests pass.
- Regeneration fuzzing (M2.9) runs without crashes on all benchmark models.

Back to the [roadmap overview](../ROADMAP.md).

---

## M2.1 — Parameters & design intent

**Size:** M · **Depends on:** M1.4

- [ ] Every dimension gets a name (`d1`, `d2`, … and renamable)
- [ ] User parameters table (Fusion's *fx* dialog): name, expression, unit, comment
- [ ] Expressions can reference other parameters and dimensions, with unit checking
- [ ] Live update while editing parameters
- [ ] **Customizer view:** chosen parameters appear as sliders, toggles and drop-downs with
      ranges and groups. Gives quick variations without touching the timeline. Also used by the
      blocks editor (M3.6).
- [ ] Favorite parameters pinned at the top

**Done when:** the enclosure benchmark is fully driven by user parameters, and its main sizes
can be changed with sliders.

## M2.2 — Patterns & mirror

**Size:** M · **Depends on:** M1.5

- [ ] Rectangular pattern, circular pattern and pattern along a path, for features, faces and
      bodies
- [ ] Mirror features and bodies across a plane or planar face
- [ ] Suppress individual pattern instances
- [ ] Fast path for identical instances (compute once, transform copies)
- [ ] Sketch-level rectangular and circular patterns and sketch mirror

## M2.3 — Advanced sketching

**Size:** L · **Depends on:** M1.4

- [ ] Offset curves (with a live distance handle)
- [ ] Polygon (inscribed, circumscribed, edge), slot (center-to-center, overall), ellipse
- [ ] Splines: fit-point and control-point, with curvature handles and tangency constraints
- [ ] Text: fonts become sketch profiles (see [Platforms](../PLATFORMS.md) for font sources)
- [ ] Project and include 3D edges, intersect bodies with the sketch plane, silhouette projection
- [ ] Import SVG and DXF into a sketch
- [ ] Auto-constrain helper that suggests constraints for a selection

## M2.4 — Advanced solid features

**Size:** XL · **Depends on:** M1.5, M2.3

- [ ] **Shell:** inside/outside thickness, open faces
- [ ] **Draft:** fixed plane, pull direction, angle
- [ ] **Loft:** multiple profiles, rails, centerline, start/end conditions
- [ ] **Sweep:** path, guide rail, twist, orientation
- [ ] **Rib**
- [ ] **Hole tool:** simple, counterbore, countersink, tapped (cosmetic thread); placed on faces
      or at sketch points; ISO metric and UNC presets
- [ ] **Thread:** modeled threads for 3D printing (with a clearance setting) and cosmetic threads;
      ISO metric presets
- [ ] **Coil** (springs)
- [ ] **Split body** (by plane, face or body) and **split face**
- [ ] **Scale body:** uniform and non-uniform
- [ ] Extrude extents: to object, all, between two faces
- [ ] **Emboss/deboss** text and profiles on planar faces (curved faces later)

**Done when:** benchmark models 1–4 of Phase 2 can be built.

## M2.5 — Direct editing II

**Size:** L · **Depends on:** M1.6 · **Risk:** depends on OCCT robustness; failures must be
clearly reported

- [ ] Delete faces with automatic healing (defeaturing)
- [ ] Remove fillets and chamfers (defeaturing)
- [ ] Move faces (translate/rotate a face; adjacent faces extend) for planar and cylindrical faces
- [ ] Press/pull on cylindrical faces (e.g. change a hole diameter)
- [ ] All of the above on imported STEP bodies, the main use case for direct editing

**Done when:** in the STEP edit benchmark, a hole is resized and a fillet removed.

## M2.6 — Components, appearances & materials

**Size:** M · **Depends on:** M1.1

- [ ] Multiple and nested components in one document; activate a component; each component has
      its own origin
- [ ] Copy/paste and linked instances of components
- [ ] Appearances: color and simple PBR materials per body or face; a small library (plastics,
      metals, wood)
- [ ] Physical materials: density for mass and center of mass
- [ ] Isolate, hide others, show all

## M2.7 — Inspection & visualization

**Size:** M · **Depends on:** M1.7

- [ ] Section analysis: live, multiple planes, capped cut faces
- [ ] Interference check between bodies
- [ ] **Overhang analysis for 3D printing:** faces steeper than a set angle are highlighted
- [ ] Draft analysis and zebra stripes (basic)
- [ ] Ambient occlusion and environment lighting (optional quality setting)
- [ ] Image export (PNG) of the viewport

## M2.8 — Interop II

**Size:** M · **Depends on:** M1.9

- [ ] STEP import with assembly structure, names and colors
- [ ] 3MF export with per-body colors and materials
- [ ] DXF and SVG export of sketches and planar faces (laser cutting)
- [ ] Mesh import (STL, OBJ, 3MF) as mesh bodies: display, measure, snap to
- [ ] Mesh to solid conversion for simple meshes (planar facets)
- [ ] glTF export (for sharing and web viewers)
- [ ] USDZ export and AR Quick Look preview (Apple only)

## M2.9 — Performance & robustness

**Size:** L · **Depends on:** M2.4

- [ ] Multithreaded booleans and fillets on native builds; WebAssembly threads where
      cross-origin isolation is available
- [ ] Incremental tessellation and level of detail for large models
- [ ] Memory budgets with monitoring on iPad and web; graceful degradation instead of crashes
- [ ] Regeneration fuzzing: random parameter edits on all benchmark models in CI
- [ ] Load-time and file-size targets for large documents in `docs/benchmarks.md`
