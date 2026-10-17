# ADR-0018: Sketch projection: linked entities that regeneration moves

- **Status:** Accepted
- **Date:** 2026-10-17
- **Milestone:** M1.3 ([#52](https://github.com/DerPW/facetry/issues/52))

## Context

Most sketches refer to geometry that already exists: a pocket inside the top face of a box, a boss
centered on the origin, a slot along an edge. Fusion and Onshape project origin items and model
edges into the sketch as linked ("projected") curves that follow the model when it changes. The
benchmark model 4 of Phase 1 needs this: an offset sketch on a box's top face.

Sketch entities are named, and other entities and features refer to them by ID
([ADR-0006](0006-persistent-naming.md), [ADR-0013](0013-sketch-model.md)). Snapping, inference and
the drawing and editing tools read a sketch's points from the document, not from the last
regeneration ([ADR-0015](0015-sketch-tools.md)). Whatever projection produces has to work with all
of that, and a curve drawn from a projected corner has to move with it.

## Options

### Projected geometry only in the regeneration result

The sketch stores what it projects; regeneration adds the projected curves to the sketch's geometry.

- Nothing projected is stored, so nothing can go stale.
- Tools that work on the document see none of it; curves cannot start on projected points, and
  constraints cannot bind them, without a second kind of entity everywhere.

### Projections that drive ordinary entities

A projection stores its source and the IDs of the line, arc or circle and the points it drives.
These are ordinary sketch entities; regeneration puts them where the source is.

- Snapping, profiles, constraints, display and the editing tools work on them unchanged. A line
  drawn from a projected corner shares the corner's point and follows it.
- Their stored coordinates are outputs, not inputs: they must not change the cache, and edits must
  leave them alone.

## Decision

Projections drive ordinary entities (`SketchProjection` in `document.hpp`, document format 4).

**Sources**

- The origin point projects as a point; the X, Y and Z axes as construction lines 1000 mm either
  side of the origin. An axis perpendicular to the sketch is refused.
- A body's edge projects with a persistent reference: lines as lines, and circles and arcs in a
  plane parallel to the sketch as circles and arcs. An edge that projects to a point, or to an
  ellipse or a spline, is refused (basic projection).
- A face projects each of its edges that can be projected. Projecting a source again adds nothing.
- Only bodies made before the sketch in the timeline can be projected.

**Regeneration**

- Before its curves, a sketch places what its projections drive where their sources lie in the
  model before it. The session writes those positions (and circle radii) back into the document
  after each regeneration, the way it writes shadows, so the tools see them. The cache key ignores
  them.
- A lost source is a warning, not an error: the projection's reference is reported as broken and
  its entities stay at their last position, so the sketch and what is built on it keep working.

**Edits**

- Trim, extend, fillet and move leave projected entities alone; trim and extend still stop at them,
  and copies of them are ordinary curves.
- Deleting a projected curve or point removes the projection. Its other points stay as ordinary
  points while other curves use them.
- `facetry_session_project` projects a list of sources as one edit. The apps' **Project** button
  projects the selected faces and edges, or with nothing selected the origin and the axes in the
  sketch's plane. Projected curves are drawn in purple.

## Consequences

- Benchmark model 4's sketch can be drawn: project the top face, draw the pocket inside it. Exact
  offsets need dimensions (M1.4); the solver will then hold the drawn curves to the projected ones.
- Editing an older sketch projects from the model at the end of the timeline. When features after
  the sketch change the projected body, its references may not resolve at the sketch's position and
  show as lost. Rolling the model back while a sketch is edited, as Fusion does, would fix this.
- Axes have a fixed length, so for very large parts their ends lie inside the model. They are
  construction lines for snapping and alignment, so this does not change profiles.
- Two projections of edges that meet have a point each at the shared corner. Snapping picks either;
  coincidence between them is implied by their sources.
