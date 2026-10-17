# ADR-0015: Sketch tools: state machines in the core, driven by pointer events on the sketch plane

- **Status:** Accepted
- **Date:** 2026-10-17
- **Milestone:** M1.3 ([#49](https://github.com/DerPW/facetry/issues/49))

## Context

Sketching needs drawing tools: lines and polylines with tangent arcs, three kinds each of
rectangle, circle and arc, and points. Each one is a small state machine:

- clicks fix points;
- a preview follows the pointer;
- the shape is written to the sketch when it is complete;
- Esc drops it.

The same tools run with a mouse, the Pencil and fingers, on the iPad, the Mac and the web.
Snapping and inference (#50) and editing (#51) build on them. If each shell had its own tools,
three platforms would drift apart in exactly the details that make sketching feel right.

## Options

### Tools in each shell

Swift and TypeScript each implement the tools and send finished entities to the core as edits.

- The shells have the pointer events directly, with no round trip for the preview.
- Two implementations of every tool, every snap rule and every inference. They will differ.

### Tools in the core

The core has a tool object. The shells convert pointer positions to sketch-plane coordinates and
pass down, move and up events; the core answers with a preview and changes the document when a
shape is complete.

- One implementation, tested in C++ like the modeling code.
- One call per pointer event. On the web that is a message to the worker, where a pending move is
  replaced by the next one so the preview keeps up.

## Decision

The tools live in the core (`SketchTool`, `facetry_sketch_tool_*`).

**Events**

- The shells ask the renderer where the pointer meets the sketch plane
  (`facetry_renderer_sketch_point`), which also says how far one screen point reaches there. A
  snap radius in millimeters is the pointer's pick radius times that.
- Events are down, move and up, in millimeters along the sketch axes:
  - down then up at about the same place is a click;
  - a drag counts as a click where it started and one where it ended, so points can be placed by
    clicking, tapping or dragging;
  - dragging back from the last point of a polyline draws an arc tangent to the polyline.

**Entities**

- Positions within the snap radius of a sketch point use that point. Shapes stay connected
  without a solver, and a polyline that comes back to its first point closes and ends.
  Snapping to midpoints, intersections and the like comes with #50.
- A completed shape is one `Session::edit`. Shells open a transaction for sketch mode (#48), so a
  whole sketch stays one undo step.
- Rectangles come out counter-clockwise, so their regions are named with `+` sides
  ([ADR-0014](0014-sketch-profiles.md)). Rectangles also store the constraints that keep them
  rectangles, for the solver (M1.4):
  - horizontal and vertical sides, for the two-corner and center tools;
  - perpendicular and parallel sides, for the three-point tool.
- Arcs are written counter-clockwise ([ADR-0013](0013-sketch-model.md)): a clockwise arc swaps its
  start and end. The center tool follows the pointer round the center, so an arc can go either way
  and past half a circle.
- New curves are construction curves when the tool says so. The `setConstruction` edit changes
  existing curves.

**Preview and input**

- The preview is a mesh like a sketch's display mesh. The renderer draws it over everything, in a
  lighter color (`facetry_renderer_set_sketch_preview`).
- The renderer also picks the sketch's curves and points before the model (`SketchCurve`,
  `SketchPoint`), for the construction toggle and later for editing.
- Input routing is in [INPUT.md](../INPUT.md#drawing-in-sketch-mode):
  - the left button, the Pencil and one finger draw;
  - Alt with the left button orbits;
  - two fingers navigate;
  - a touch waits until it moves or lifts, so a second finger can still make it a navigation
    gesture.

## Consequences

- Snapping and inference (#50) extend `SketchTool::snap` and the preview; nothing changes in the
  shells but the glyphs they show.
- Editing tools (#51) can follow the same pattern: a tool object with pointer events.
- Every pointer move crosses the C API. That is cheap natively. On the web the worker round trip
  adds a frame or two to the preview, which collapsing pending moves keeps from piling up.
- A tool reads the sketch's points from the document, not the last regeneration, so it snaps to
  what it drew a moment ago before the model catches up.
