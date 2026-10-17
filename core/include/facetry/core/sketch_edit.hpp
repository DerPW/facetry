// Editing sketches (#51): trim, extend, fillet, delete, move and copy, and projecting geometry into
// them (#52). Each changes a document; sessions apply them as edits, the sketch tools
// (sketch_tool.hpp) from pointer events. Positions and distances are in millimeters on the sketch
// plane. Edits leave the curves and points that projections drive alone, except delete.
#pragma once

#include "facetry/core/document.hpp"
#include "facetry/core/regeneration.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace facetry::core {

// Deletes points and curves of the sketch feature `sketch`, and the constraints on them. Deleting a
// point deletes the curves that use it; points that only deleted curves used go too. Throws
// Error(InvalidArgument) if the sketch or an entity does not exist.
void deleteSketchEntities(Document& document, const std::string& sketch,
                          const std::vector<std::string>& entities);

// Trims the curve nearest to (u, v), within `radius`: the piece between the curves crossing it on
// either side of (u, v) goes, or the whole curve if nothing crosses it. A line or arc keeps its ID
// on the piece from its start; a circle cut at two places or more becomes an arc with the circle's
// ID. New ends lie on the curves that cut them (coincident constraints). Returns false if no curve
// is near.
bool trimSketch(Document& document, const std::string& sketch, double u, double v, double radius);

// Extends the line nearest to (u, v), within `radius`, from its end nearer to (u, v) to the first
// curve ahead. Returns false if no line is near or nothing lies ahead.
bool extendSketch(Document& document, const std::string& sketch, double u, double v, double radius);

// Rounds the corner `point`, where exactly two lines end, with an arc tangent to both that starts
// `setback` from the corner along each line. Returns false if the point is no such corner or the
// setback is longer than a line.
bool filletSketchCorner(Document& document, const std::string& sketch, const std::string& point,
                        double setback);

// Moves points and curves (all their points) by (du, dv); with `copy`, adds moved copies of them
// instead. Values that named a parameter become numbers.
void moveSketchEntities(Document& document, const std::string& sketch,
                        const std::vector<std::string>& entities, double du, double dv, bool copy);

// What to project into a sketch: an origin item ("o", "x", "y" or "z"), or an edge or a face of a
// body (`kind` and its element ID in the body); a face projects its edges.
struct ProjectionSource {
    std::string origin{};
    std::string body{};
    ElementKind kind = ElementKind::Edge;
    std::uint32_t id = 0;
};

// Projects `sources` into the sketch feature `sketch` (ADR-0018): each origin item and edge gets a
// projection driving a new curve and its points (the origin a point), where the source lies in
// `model`, the last regeneration. Axes become construction lines. Sources the sketch projects
// already are skipped, and so are a face's edges that cannot be projected. Returns how many
// projections were added. Throws Error(InvalidArgument) if the sketch, a body or an element does
// not exist, the body comes after the sketch in the timeline, or an edge cannot be projected.
std::size_t projectIntoSketch(Document& document, const ModelState& model,
                              const std::string& sketch, std::span<const ProjectionSource> sources);

}  // namespace facetry::core
