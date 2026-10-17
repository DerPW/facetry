# ADR-0017: Sketch editing: core edits on IDs, tools that preview them

- **Status:** Accepted
- **Date:** 2026-10-17
- **Milestone:** M1.3 ([#51](https://github.com/DerPW/facetry/issues/51))

## Context

Drawing alone does not make a usable sketcher. Profiles are rarely drawn exactly: a line runs past
the edge it should stop at, a corner needs rounding, a shape belongs a little further left. Fusion
and Shapr3D have trim, extend, sketch fillet, delete and move/copy for this. The benchmark sketches
of M1.3 need them, for example to round an L-profile's corners or trim an overlong line.

Sketch entities are named, not indexed ([ADR-0006](0006-persistent-naming.md)): profiles, regions
and the faces of extrusions carry curve IDs. An edit that rebuilds the curves under new IDs would
break every reference to them. The sketch tools already live in the core
([ADR-0015](0015-sketch-tools.md)).

## Options

### Rebuild the touched curves

Delete the curves an edit touches and draw new ones.

- Simple to implement: every edit is a delete plus a drawing.
- New IDs: an extrusion's chosen regions and everything downstream lose their references on every
  trim.

### Edit the curves in place

Change a curve's points, or turn a circle into an arc, keeping its ID wherever the result is
recognisably the same curve. Only what is genuinely new gets a new ID.

- References survive the common edits (trimming an overshoot, extending, rounding a corner).
- The rules for which piece keeps the ID have to be stated, and tested.

## Decision

Edits change curves in place (`sketch_edit.hpp`), and keep IDs as follows:

- **Trim** removes the piece of the curve under the pointer between the nearest curves crossing it
  on either side, or the whole curve if nothing crosses it.
  - A line or arc keeps its ID on the piece from its start; the piece after a removed middle is new.
    It keeps the line's direction constraints (horizontal, vertical, parallel, perpendicular), or
    gets one parallel to the line.
  - A circle cut at two places or more becomes an arc with the circle's ID; with fewer cuts it goes.
  - New ends reuse a sketch point already there, or get a new point coincident with the curves
    that cut them.
- **Extend** moves a line's nearer end to the first curve ahead, coincident with it.
- **Fillet** rounds a corner where exactly two lines end, with an arc tangent to both (tangent
  constraints). The setback is the distance from the corner to where the arc starts on each line;
  for a corner of angle θ the radius is the setback × tan(θ/2). The lines keep their IDs.
- **Delete** removes points and curves, the curves on deleted points, the points nothing uses any
  more, and every constraint on what went.
- **Move** moves points and the points of curves; curves sharing a moved point stretch. **Copy**
  adds moved copies of the curves, their points and the constraints among them. Moved coordinates
  become numbers, even where they named a parameter.

**Tools**

- Trim, extend, fillet, move and copy are `SketchTool` kinds 11–15, driven by the same pointer
  events as the drawing tools. Trim and extend act where the pointer is, without snapping, since
  snapping would move it onto the intersections that bound the pieces. Fillet, move and copy snap
  like the drawing tools, so a copy can land exactly on a point.
- Each tool previews its edit from a plan that the edit itself carries out
  (`sketch_edit_plan.hpp`): the piece a trim removes as a dashed guide, the segment an extend adds,
  the fillet arc, the moved curves. The fillet radius at the pointer comes with the snap feedback.
- Move and copy take a selection (`facetry_sketch_tool_set_selection`); without one, the first
  click takes the point or curve under it, so dragging a line moves it.
- Delete is a session edit (`deleteSketchEntities`), from a button or the `Delete` key.

## Consequences

- Extrusions keep their regions across the usual edits; the region names follow the curve IDs
  ([ADR-0014](0014-sketch-profiles.md)).
- Moving a point leaves constraints that no longer hold (a coincidence moved off its curve) until
  the solver (M1.4) enforces them again. Copies of constraints keep a copy self-consistent.
- After a trim in the middle of a line, a reference to the line follows the piece from its start;
  the other piece is a new line.
- Fillets only round corners of two lines. Line–arc corners and fillets with a typed radius come
  with dimensions (M1.4).
