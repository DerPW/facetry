// Sketch geometry for the drawing and editing tools, internal to the core: a sketch's points and
// curves in millimeters on its plane, nearest points and intersections.
#pragma once

#include "facetry/core/document.hpp"
#include "facetry/core/error.hpp"
#include "facetry/core/naming.hpp"
#include "facetry/core/regeneration.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <numbers>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace facetry::core::sketching {

using Vector = std::pair<double, double>;

constexpr double kEpsilon = 1e-9;

inline double distance(double u0, double v0, double u1, double v1) {
    return std::hypot(u1 - u0, v1 - v0);
}

inline Vector normalized(double du, double dv) {
    const double length = std::hypot(du, dv);
    return length > kEpsilon ? Vector{du / length, dv / length} : Vector{0.0, 0.0};
}

inline const Sketch& sketchOf(const Document& document, const std::string& id) {
    const Feature* feature = document.findFeature(id);
    if (feature == nullptr || !std::holds_alternative<Sketch>(feature->data)) {
        throw Error(ErrorCode::InvalidArgument, "The sketch " + id + " does not exist");
    }
    return std::get<Sketch>(feature->data);
}

// The sketch's points in millimeters, from the document: the last regeneration can lag behind the
// tool's own edits. Points on a missing parameter are left out.
struct PlacedPoint {
    std::string id;
    double u = 0.0;
    double v = 0.0;
};

inline std::vector<PlacedPoint> pointsOf(const Document& document, const Sketch& sketch) {
    std::vector<PlacedPoint> points;
    for (const SketchPoint& point : sketch.points) {
        try {
            points.push_back({point.id, lengthInMillimeters(point.x, document),
                              lengthInMillimeters(point.y, document)});
        } catch (const Error&) {
        }
    }
    return points;
}

// The circle through three points: center and radius; nothing if they lie on one line.
inline std::optional<std::tuple<double, double, double>> circleThrough(double ax, double ay,
                                                                       double bx, double by,
                                                                       double cx, double cy) {
    const double d = 2.0 * (ax * (by - cy) + bx * (cy - ay) + cx * (ay - by));
    const double size = std::max({distance(ax, ay, bx, by), distance(bx, by, cx, cy), 1.0});
    if (std::abs(d) < 1e-9 * size * size) {
        return std::nullopt;
    }
    const double a = ax * ax + ay * ay;
    const double b = bx * bx + by * by;
    const double c = cx * cx + cy * cy;
    const double ux = (a * (by - cy) + b * (cy - ay) + c * (ay - by)) / d;
    const double uy = (a * (cx - bx) + b * (ax - cx) + c * (bx - ax)) / d;
    return std::tuple{ux, uy, distance(ux, uy, ax, ay)};
}

// The angle counter-clockwise from `from` to `to`, in [0, 2π).
inline double ccwAngle(double from, double to) {
    const double turn = std::fmod(to - from, 2.0 * std::numbers::pi);
    return turn < 0.0 ? turn + 2.0 * std::numbers::pi : turn;
}

// The arc that leaves `start` in `direction` and ends at `end`: its center and whether it runs
// counter-clockwise. Nothing if `end` lies straight ahead or behind.
inline std::optional<std::tuple<double, double, bool>> tangentArc(double su, double sv,
                                                                  Vector direction, double eu,
                                                                  double ev) {
    const double nu = -direction.second;  // left of the direction
    const double nv = direction.first;
    const double du = eu - su;
    const double dv = ev - sv;
    const double across = 2.0 * (nu * du + nv * dv);
    if (std::abs(across) < 1e-9 * std::max(1.0, std::hypot(du, dv))) {
        return std::nullopt;
    }
    const double radius = (du * du + dv * dv) / across;  // negative: the center lies to the right
    return std::tuple{su + nu * radius, sv + nv * radius, radius > 0.0};
}

// The direction of travel at the end of an arc around (cu, cv) that runs counter-clockwise if
// `ccw`.
inline Vector arcDirectionAt(double cu, double cv, double u, double v, bool ccw) {
    const Vector ccwTangent = normalized(-(v - cv), u - cu);
    return ccw ? ccwTangent : Vector{-ccwTangent.first, -ccwTangent.second};
}

inline ProfileSegment lineSegment(double u0, double v0, double u1, double v1) {
    return {.name = {}, .x0 = u0, .y0 = v0, .x1 = u1, .y1 = v1};
}

inline ProfileSegment arcSegment(double cu, double cv, double u0, double v0, double u1, double v1,
                                 bool ccw) {
    return {.name = {},
            .x0 = u0,
            .y0 = v0,
            .x1 = u1,
            .y1 = v1,
            .kind = ProfileSegment::Kind::Arc,
            .cx = cu,
            .cy = cv,
            .radius = 0.0,
            .clockwise = !ccw};
}

inline ProfileSegment circleSegment(double cu, double cv, double radius) {
    return {.name = {}, .kind = ProfileSegment::Kind::Circle, .cx = cu, .cy = cv, .radius = radius};
}

// The sketch's curves in millimeters, for snapping. Arcs are circles with a sweep.
struct SnapLine {
    std::string id;
    double u0 = 0.0;
    double v0 = 0.0;
    double u1 = 0.0;
    double v1 = 0.0;
};

struct SnapCircle {
    std::string id;
    double cu = 0.0;
    double cv = 0.0;
    double radius = 0.0;
    bool arc = false;
    double start = 0.0;  // arcs: counter-clockwise from `start` by `sweep`
    double sweep = 0.0;

    [[nodiscard]] bool covers(double angle) const {
        return !arc || ccwAngle(start, angle) <= sweep + 1e-9;
    }
};

struct SnapGeometry {
    std::vector<PlacedPoint> points;
    std::set<std::string> centers;
    std::vector<SnapLine> lines;
    std::vector<SnapCircle> circles;
};

inline SnapGeometry snapGeometry(const Document& document, const Sketch& sketch) {
    SnapGeometry geometry;
    geometry.points = pointsOf(document, sketch);
    std::map<std::string, std::pair<double, double>> at;
    for (const PlacedPoint& point : geometry.points) {
        at[point.id] = {point.u, point.v};
    }
    for (const SketchLine& line : sketch.lines) {
        if (at.contains(line.start) && at.contains(line.end)) {
            const auto [u0, v0] = at[line.start];
            const auto [u1, v1] = at[line.end];
            geometry.lines.push_back({line.id, u0, v0, u1, v1});
        }
    }
    for (const SketchArc& arc : sketch.arcs) {
        if (at.contains(arc.center) && at.contains(arc.start) && at.contains(arc.end)) {
            const auto [cu, cv] = at[arc.center];
            const auto [su, sv] = at[arc.start];
            const auto [eu, ev] = at[arc.end];
            const double start = std::atan2(sv - cv, su - cu);
            geometry.circles.push_back({arc.id, cu, cv, distance(cu, cv, su, sv), true, start,
                                        ccwAngle(start, std::atan2(ev - cv, eu - cu))});
            geometry.centers.insert(arc.center);
        }
    }
    for (const SketchCircle& circle : sketch.circles) {
        if (!at.contains(circle.center)) {
            continue;
        }
        try {
            const auto [cu, cv] = at[circle.center];
            geometry.circles.push_back(
                {circle.id, cu, cv, lengthInMillimeters(circle.radius, document)});
            geometry.centers.insert(circle.center);
        } catch (const Error&) {
        }
    }
    return geometry;
}

// The nearest point of a segment.
inline std::pair<double, double> nearestOn(const SnapLine& line, double u, double v) {
    const double du = line.u1 - line.u0;
    const double dv = line.v1 - line.v0;
    const double length = du * du + dv * dv;
    const double t = length > 0.0
                         ? std::clamp(((u - line.u0) * du + (v - line.v0) * dv) / length, 0.0, 1.0)
                         : 0.0;
    return {line.u0 + t * du, line.v0 + t * dv};
}

// The nearest point of a circle or arc, if the arc reaches round to it.
inline std::optional<std::pair<double, double>> nearestOn(const SnapCircle& circle, double u,
                                                          double v) {
    const double angle = std::atan2(v - circle.cv, u - circle.cu);
    if (distance(u, v, circle.cu, circle.cv) < kEpsilon || !circle.covers(angle)) {
        return std::nullopt;
    }
    return std::pair{circle.cu + circle.radius * std::cos(angle),
                     circle.cv + circle.radius * std::sin(angle)};
}

// The curve nearest to (u, v) within `radius`: a line, a circle or arc, or neither.
struct NearestCurve {
    const SnapLine* line = nullptr;
    const SnapCircle* circle = nullptr;

    [[nodiscard]] std::string id() const {
        return line != nullptr ? line->id : circle != nullptr ? circle->id : std::string();
    }
};

inline NearestCurve nearestCurve(const SnapGeometry& geometry, double u, double v, double radius,
                                 bool linesOnly = false) {
    NearestCurve nearest;
    double best = radius;
    for (const SnapLine& line : geometry.lines) {
        const auto [qu, qv] = nearestOn(line, u, v);
        if (distance(u, v, qu, qv) <= best) {
            best = distance(u, v, qu, qv);
            nearest = {.line = &line};
        }
    }
    for (const SnapCircle& circle : geometry.circles) {
        if (linesOnly) {
            break;
        }
        const auto q = nearestOn(circle, u, v);
        if (q && distance(u, v, q->first, q->second) <= best) {
            best = distance(u, v, q->first, q->second);
            nearest = {.circle = &circle};
        }
    }
    return nearest;
}

// Where a line meets a circle or another line, within both.
inline std::vector<std::pair<double, double>> intersections(const SnapLine& a, const SnapLine& b) {
    const double du = a.u1 - a.u0;
    const double dv = a.v1 - a.v0;
    const double eu = b.u1 - b.u0;
    const double ev = b.v1 - b.v0;
    const double cross = du * ev - dv * eu;
    if (std::abs(cross) < kEpsilon) {
        return {};
    }
    const double t = ((b.u0 - a.u0) * ev - (b.v0 - a.v0) * eu) / cross;
    const double s = ((b.u0 - a.u0) * dv - (b.v0 - a.v0) * du) / cross;
    if (t < -1e-9 || t > 1 + 1e-9 || s < -1e-9 || s > 1 + 1e-9) {
        return {};
    }
    return {{a.u0 + t * du, a.v0 + t * dv}};
}

inline std::vector<std::pair<double, double>> intersections(const SnapLine& line,
                                                            const SnapCircle& circle) {
    const double du = line.u1 - line.u0;
    const double dv = line.v1 - line.v0;
    const double fu = line.u0 - circle.cu;
    const double fv = line.v0 - circle.cv;
    const double a = du * du + dv * dv;
    const double b = 2 * (fu * du + fv * dv);
    const double c = fu * fu + fv * fv - circle.radius * circle.radius;
    const double discriminant = b * b - 4 * a * c;
    if (a < kEpsilon || discriminant < 0) {
        return {};
    }
    std::vector<std::pair<double, double>> result;
    for (const double sign : {-1.0, 1.0}) {
        const double t = (-b + sign * std::sqrt(discriminant)) / (2 * a);
        const double u = line.u0 + t * du;
        const double v = line.v0 + t * dv;
        if (t >= -1e-9 && t <= 1 + 1e-9 &&
            circle.covers(std::atan2(v - circle.cv, u - circle.cu))) {
            result.emplace_back(u, v);
        }
    }
    return result;
}

inline std::vector<std::pair<double, double>> intersections(const SnapCircle& a,
                                                            const SnapCircle& b) {
    const double d = distance(a.cu, a.cv, b.cu, b.cv);
    if (d < kEpsilon || d > a.radius + b.radius || d < std::abs(a.radius - b.radius)) {
        return {};
    }
    const double along = (a.radius * a.radius - b.radius * b.radius + d * d) / (2 * d);
    const double across = std::sqrt(std::max(0.0, a.radius * a.radius - along * along));
    const double mu = a.cu + along * (b.cu - a.cu) / d;
    const double mv = a.cv + along * (b.cv - a.cv) / d;
    std::vector<std::pair<double, double>> result;
    for (const double sign : {-1.0, 1.0}) {
        const double u = mu + sign * across * (b.cv - a.cv) / d;
        const double v = mv - sign * across * (b.cu - a.cu) / d;
        if (a.covers(std::atan2(v - a.cv, u - a.cu)) && b.covers(std::atan2(v - b.cv, u - b.cu))) {
            result.emplace_back(u, v);
        }
    }
    return result;
}

}  // namespace facetry::core::sketching
