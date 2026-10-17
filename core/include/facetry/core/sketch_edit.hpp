// Editing sketches (#51): trim, extend, fillet, delete, move and copy. Each changes a document;
// sessions apply them as edits, the sketch tools (sketch_tool.hpp) from pointer events. Positions
// and distances are in millimeters on the sketch plane.
#pragma once

#include "facetry/core/document.hpp"

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

}  // namespace facetry::core
