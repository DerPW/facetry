# ADR-0013: Sketch model: entities on shared points, a plane by name or face reference

- **Status:** Accepted
- **Date:** 2026-10-17
- **Milestone:** M1.3 ([#46](https://github.com/DerPW/facetry/issues/46))

## Context

The M0.6 sketch holds points and lines on XY that form one closed loop. M1.3 needs more:

- sketches on the other origin planes and on planar faces of bodies;
- arcs, circles and construction geometry;
- constraints, which the solver ([ADR-0005](0005-sketch-solver-planegcs.md)) solves from M1.4.

Later features reference sketch curves by ID (the side faces of an extrusion are named after
them, [ADR-0006](0006-persistent-naming.md)), so the IDs must stay stable while the user draws,
trims and edits. Profile detection (#47) has to find which curves meet. The drawing tools,
snapping and inference (#49, #50) need the same connectivity.

## Options

### Entities: curves with their own coordinates, joined by constraints

Each curve stores its own end coordinates, and coincident constraints join them. FreeCAD's
Sketcher works this way.

- Every corner of a rectangle needs a coincident constraint; a curve can be moved away from its
  neighbor without the solver.
- Finding connected curves means checking constraints or comparing coordinates within a
  tolerance.

### Entities: curves on shared points

Points are entities of their own. A curve refers to its points by ID, and two curves that meet
share a point. SolveSpace works this way.

- Connectivity is exact and cheap: two curves meet where they share a point ID. Profiles,
  trimming and snapping use it.
- A rectangle is four points, four lines and no coincident constraints.
- The solver works on points: dragging a corner moves both lines.
- Splitting a curve (trim, #51) adds a point and keeps both pieces attached.

### Sketch plane: a stored frame

The sketch stores its origin and axes in space.

- Simple and always the same.
- A sketch on a face does not follow the face when the model changes, which is the main reason to
  sketch on a face.

### Sketch plane: an origin plane by name, or a face reference

The sketch names an origin plane or refers to a planar face with a persistent reference, like a
fillet's edges.

- The sketch follows the face, and a lost face shows up as a broken reference with the usual
  repair UI (M1.1).
- The sketch's in-plane axes come from the face's surface.

## Decision

Curves sit on shared points, and the plane is an origin plane or a face reference.

**Entities**

- Points, lines, arcs, circles and constraints, all with IDs from the document's one counter.
  IDs are unique within the sketch, and points and curves share one namespace.
- A line has a start and an end point.
- An arc has a center, a start and an end point. It runs counter-clockwise from start to end,
  seen from the side the normal points to. The end must lie on the circle through the start;
  regeneration checks this until the solver keeps it there (M1.4).
- A circle has a center point and a radius value, which can name a parameter.
- Lines, arcs and circles have a `construction` flag. Construction curves help drawing and never
  bound a profile.
- A constraint has a type and the IDs of the points and curves it binds. Types:
  - coincident, horizontal, vertical, parallel, perpendicular;
  - tangent, equal, midpoint, concentric, fix.

  Validation checks the type, the number of entities and that each one exists. Constraints are
  kept but not solved until M1.4. Dimensions are constraints with a value and arrive with the
  solver.

**Plane**

- `"plane"`: `"xy"`, `"xz"` or `"yz"`, oriented like Fusion's origin planes. The normal is x × y:
  +Z, −Y and +X.
- `"face"` with one face reference. The frame comes from the face's plane, with the normal
  pointing out of the body. The reference resolves like any other: by name, then by its shadow.

**Regeneration**

- A sketch adds its plane and its curves, named `<sketch>/<curve>`, to the model.
- An extrusion takes one closed loop of non-construction lines and arcs, or a single circle,
  and extrudes it along the sketch normal. Side faces are named `<extrude>:side[<sketch>/<curve>]`.
- Choosing among several profiles comes with profile detection (#47).

**Format and API**

- Document format version 3. Version 1 and 2 sketches load as XY sketches.
- `"plane"` is always written. `"face"`, `"arcs"`, `"circles"` and `"constraints"` are written
  only when present, and `"construction"` only when true.
- The feature definitions of the session JSON (ADR-0011) carry the same fields: API 0.8.

## Consequences

- Older Facetry versions reject version 3 documents as newer.
- Profile detection (#47) can build regions from shared points and the curves' geometry. An
  extrusion will then name the regions it uses instead of taking the only loop.
- Trim, extend and the sketch fillet (#51) split or join curves by adding and removing points. The
  curve that keeps the original ID has to be chosen carefully: the names of later faces depend on
  it.
- A face sketch's in-plane axes come from OCCT's parametrization of the face. For extrusion caps
  they are the sketch's own axes and stay put. For faces of later operations, such as Booleans,
  they could turn when the model changes. If that happens, the sketch can store a hint for its
  x-direction, for example the edge it was aligned to.
- Adding a constraint type is a format change: older versions reject unknown types.
