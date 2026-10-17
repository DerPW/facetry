# ADR-0016: Sketch snapping and inference: ranked snaps that leave constraints behind

- **Status:** Accepted
- **Date:** 2026-10-17
- **Milestone:** M1.3 ([#50](https://github.com/DerPW/facetry/issues/50))

## Context

M1.3's goal is to draw every benchmark sketch without typing coordinates. Without help, a line
drawn by hand is never quite horizontal and never quite meets the middle of another one. Fusion
and Shapr3D snap to existing geometry, infer directions while a line is drawn, and keep the
result: a snapped point stays on its curve and an inferred horizontal stays horizontal. The
solver (M1.4) can only keep what the sketch records as constraints.

The drawing tools ([ADR-0015](0015-sketch-tools.md)) already run in the core and snap to existing
points. This decides how far snapping goes and what it leaves behind.

## Options

### Snap only, keep nothing

Move the pointer to the snap target and forget why.

- Simple.
- The first edit or solver run separates what only looked connected.

### Snap and record the reason as constraints

Every snap that is not onto an existing point adds the constraint that explains it. A point on
the middle of a line gets a midpoint constraint, an inferred horizontal line a horizontal one.

- What the user saw while drawing is what the solver keeps.
- The sketch gets many constraints, so they need glyphs to stay visible and, later, removable.

## Decision

Snaps are ranked and leave constraints behind.

**Snaps** are taken within the snap radius (the pointer's pick radius, [ADR-0015](0015-sketch-tools.md)),
nearest first within a rank:

1. Existing points: endpoints and centers. The shape uses the point itself.
2. Midpoints of lines, quadrants of circles and arcs, intersections of curves near the pointer,
   and the sketch origin. A new point there gets a midpoint constraint, or a coincident constraint
   with each curve it lies on.
3. Anywhere on a curve: a new point with a coincident constraint.
4. Free positions:
   - the line tool infers a direction from the line's start: horizontal or vertical first, then
     tangent to the arc or perpendicular to the line it leaves, then parallel or perpendicular to
     lines near the pointer;
   - the line gets the matching constraint;
   - a free position also lines up horizontally or vertically with existing points (alignment
     guides, without constraints);
   - with **grid snap** on, positions with neither round to the grid's current minor spacing.

Tangent arcs from a polyline and from the tangent arc tool get a tangent constraint.

**Feedback**

- The tool reports what the pointer snapped to and what the next click infers
  (`facetry_sketch_tool_feedback`), shown as a label next to the pointer.
- Guides come in the preview as edges from ID 1001 on, drawn dashed.
- Constraints show as glyphs on the sketch. Their positions come from the core in the session
  state (`"glyphs"` of a sketch). The shells place text labels there every frame
  (`facetry_renderer_project`):

  | Constraint | Glyph | Constraint | Glyph |
  |---|---|---|---|
  | horizontal | H | equal | = |
  | vertical | V | coincident | ⊙ |
  | parallel | ∥ | midpoint | △ |
  | perpendicular | ⊥ | concentric | ◎ |
  | tangent | T | fix | F |

The origin snap places a point at the sketch origin without a constraint, until origin geometry
can be projected into the sketch (#52).

## Consequences

- Sketches carry the constraints the solver needs from the start; M1.4 adds dimensions and solving
  but no retrofitting.
- Inference can surprise: a line meant to be slightly off horizontal snaps flat. Drawing it outside
  the snap radius, or removing the constraint later (M1.4), handles that. A modifier key that
  turns snapping off is a candidate for M1.8.
- Snapping considers all curves and points of the sketch on every pointer move, plus intersections
  of the curves near the pointer. This is fast for sketches of a few hundred curves; larger ones
  would need a spatial index.
- Glyphs are text labels in the shells, not rendered geometry, so they stay crisp at any zoom and
  cost nothing in the renderer.
