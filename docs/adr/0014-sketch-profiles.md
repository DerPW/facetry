# ADR-0014: Sketch profiles: regions from splitting the sketch plane, named by the curves around them

- **Status:** Accepted
- **Date:** 2026-10-17
- **Milestone:** M1.3 ([#47](https://github.com/DerPW/facetry/issues/47))

## Context

Up to [ADR-0013](0013-sketch-model.md), an extrusion took the sketch's one closed loop. Fusion
works with *profiles*: the closed regions that the sketch curves enclose, including where curves
cross and with islands nested inside. A rectangle with a circle inside has two profiles, the plate
with a hole and the disc, and an extrusion takes any of them.

An extrusion has to remember which profiles it takes across edits. The same rules apply as for
faces and edges ([ADR-0006](0006-persistent-naming.md)):

- names come from history and roles, never from indices;
- an edit elsewhere in the sketch must not rename a region;
- a region that is really gone fails the feature visibly.

## Options

### Finding regions: our own planar graph

Intersect the curves ourselves, build a planar graph and walk its faces.

- Full control, with no kernel calls for 2D work.
- Robust intersection of lines and arcs, including tangencies, overlaps and tolerances, is exactly
  what a geometry kernel already does well. Nesting and holes need more work on top.

### Finding regions: OpenCASCADE's splitter

Lay a sheet face on the sketch plane well beyond the curves and split it with the curves
(`BRepAlgoAPI_Splitter`, the General Fuse algorithm). Every piece is a region, except the one
touching the sheet's border.

- Intersections, overlaps, tangencies, holes and islands come from the kernel's Boolean code.
- Curves that enclose nothing, such as an open line inside a region, become internal edges. They
  are left out.
- The pieces are faces already, ready to extrude, tessellate and pick.
- A Boolean operation per sketch regeneration, which is cheap for sketch-sized input.

### Naming regions: by index or seed point

- An index changes whenever a curve is added anywhere.
- A point inside the region does not survive moving the geometry.

### Naming regions: by the curves around them and the side

The name is the sorted set of the bounding curves, each with the side the region lies on:
`region{f1/c10-|f1/l6+|f1/l7+|f1/l8+|f1/l9+}`. `+` means left of the curve's direction (lines
from start to end, arcs and circles counter-clockwise), `-` means right of it.

- Adding a curve elsewhere, or changing sizes and positions, keeps the name.
- Splitting a region or drawing a curve the other way round changes it, and the extrusion
  reports the lost profile.
- The side tells the two regions on either side of a curve apart: the disc is `{c10+}`, the plate
  around it has `c10-`.

## Decision

Regions come from OpenCASCADE's splitter and are named by their bounding curves and sides.

**Regions**

- `sketchRegions()` in the kernel adapter splits the sheet face with the non-construction curves.
  Degenerate curves are ignored. Each region keeps, per edge, the curve the edge is a piece of.
  The curve is found geometrically: the nearest curve through the edge's middle.
- Equal names get `#1`, `#2`, … in geometric order, like faces (ADR-0006).
- Regions are sorted by name. A sketch's result in the model holds its regions. The session
  summary lists their names, and `facetry_session_sketch_regions` returns their faces as one shape
  for display and picking (API 0.9).

**Extrusion**

- An extrusion names the regions it takes in `"regions"`. Without it, the sketch's only region is
  taken, so earlier documents keep working. A sketch with several regions then asks the user to
  choose.
- Adjacent chosen regions merge into one face: their shared edges are dropped and the faces are
  rebuilt with `BOPAlgo_BuilderFace`.
- Pieces of one curve that meet only each other are joined again
  (`ShapeUpgrade_UnifySameDomain`), so one sketch line gives one side face.
- Side faces are named after the curve, as before: `f2:side[f1/l7]`.

**Viewport**

- The renderer draws a sketch's regions over the model, flat and translucent, with outlines. It
  picks them before the model as `FACETRY_PICK_REGION`. The filter `FACETRY_PICK_REGIONS` picks only
  regions.
- In the apps, "Choose profiles" in an extrusion's details shows its sketch's regions with the
  current choice selected. Clicks and taps add or remove regions, and Done saves the choice as one
  undo step.

## Consequences

- The format version stays 3: `"regions"` is optional and came within the same milestone, before
  any release wrote version 3 documents.
- Region names are only as stable as the curve IDs. Trim and split (#51) must keep the original ID
  on the piece that keeps the region's shape.
- Splitting a region in two (say, a line drawn across it) loses the old name. A later repair could
  offer the pieces whose names contain the old one's curves.
- Projected geometry (#52) and constraints (M1.4) do not change the scheme: projections are curves
  with IDs of their own.
- The sketch overlay in the renderer is where sketch mode (#48) can draw curves and points.
