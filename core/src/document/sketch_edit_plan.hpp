// What a sketch edit would do (sketch_edit.hpp), internal to the core: the editing functions
// carry out these plans, and the sketch tools show them as previews.
#pragma once

#include "facetry/core/document.hpp"
#include "facetry/core/naming.hpp"

#include <optional>
#include <string>
#include <vector>

namespace facetry::core::sketching {

// A place where other curves cut a curve: its parameter on the curve (0 to 1 along a line, the
// angle from an arc's start, the angle of a circle), its position and the curves through it.
struct Cut {
    double at = 0.0;
    double u = 0.0;
    double v = 0.0;
    std::vector<std::string> curves{};
};

// The piece of a curve that trimming removes: between `from` and `to`, or from the curve's start
// or to its end when one is missing; the whole curve when both are.
struct TrimPlan {
    std::string curve;
    std::optional<Cut> from{};
    std::optional<Cut> to{};
    ProfileSegment removed{};
};

// A line's end moving to the first curve ahead.
struct ExtendPlan {
    std::string line;
    bool atStart = false;
    Cut hit{};
    ProfileSegment added{};
};

// An arc rounding the corner `point` of the lines `first` and `second`, from (u1, v1) on the first
// to (u2, v2) on the second, around (cu, cv).
struct FilletPlan {
    std::string point;
    std::string first;
    std::string second;
    double u1 = 0.0;
    double v1 = 0.0;
    double u2 = 0.0;
    double v2 = 0.0;
    double cu = 0.0;
    double cv = 0.0;
    bool ccw = true;
    ProfileSegment arc{};
};

[[nodiscard]] std::optional<TrimPlan> planTrim(const Document& document, const Sketch& sketch,
                                               double u, double v, double radius);
[[nodiscard]] std::optional<ExtendPlan> planExtend(const Document& document, const Sketch& sketch,
                                                   double u, double v, double radius);
[[nodiscard]] std::optional<FilletPlan> planFillet(const Document& document, const Sketch& sketch,
                                                   const std::string& point, double setback);
// The curves that moving `entities` by (du, dv) moves, moved.
[[nodiscard]] std::vector<ProfileSegment> movedCurves(const Document& document,
                                                      const Sketch& sketch,
                                                      const std::vector<std::string>& entities,
                                                      double du, double dv);

}  // namespace facetry::core::sketching
