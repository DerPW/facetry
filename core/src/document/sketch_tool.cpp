#include "facetry/core/sketch_tool.hpp"

#include "facetry/core/error.hpp"
#include "facetry/core/regeneration.hpp"

#include <cmath>
#include <numbers>
#include <tuple>

namespace facetry::core {
namespace {

using Vector = std::pair<double, double>;

constexpr double kEpsilon = 1e-9;

double distance(double u0, double v0, double u1, double v1) {
    return std::hypot(u1 - u0, v1 - v0);
}

Vector normalized(double du, double dv) {
    const double length = std::hypot(du, dv);
    return length > kEpsilon ? Vector{du / length, dv / length} : Vector{0.0, 0.0};
}

const Sketch& sketchOf(const Document& document, const std::string& id) {
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

std::vector<PlacedPoint> pointsOf(const Document& document, const Sketch& sketch) {
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
std::optional<std::tuple<double, double, double>> circleThrough(double ax, double ay, double bx,
                                                                double by, double cx, double cy) {
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
double ccwAngle(double from, double to) {
    const double turn = std::fmod(to - from, 2.0 * std::numbers::pi);
    return turn < 0.0 ? turn + 2.0 * std::numbers::pi : turn;
}

// The arc that leaves `start` in `direction` and ends at `end`: its center and whether it runs
// counter-clockwise. Nothing if `end` lies straight ahead or behind.
std::optional<std::tuple<double, double, bool>> tangentArc(double su, double sv, Vector direction,
                                                           double eu, double ev) {
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
Vector arcDirectionAt(double cu, double cv, double u, double v, bool ccw) {
    const Vector ccwTangent = normalized(-(v - cv), u - cu);
    return ccw ? ccwTangent : Vector{-ccwTangent.first, -ccwTangent.second};
}

ProfileSegment lineSegment(double u0, double v0, double u1, double v1) {
    return {.name = {}, .x0 = u0, .y0 = v0, .x1 = u1, .y1 = v1};
}

ProfileSegment arcSegment(double cu, double cv, double u0, double v0, double u1, double v1,
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

ProfileSegment circleSegment(double cu, double cv, double radius) {
    return {.name = {}, .kind = ProfileSegment::Kind::Circle, .cx = cu, .cy = cv, .radius = radius};
}

// Adds entities to a sketch, with IDs from the document and values in its units.
class Builder {
  public:
    Builder(Document& document, Sketch& sketch, bool construction)
        : document_(document),
          sketch_(sketch),
          construction_(construction),
          scale_(millimetersPer(document.units)) {}

    // The existing point `id`, or a new point at (u, v).
    std::string point(double u, double v, const std::string& id = {}) {
        if (!id.empty()) {
            return id;
        }
        std::string created = document_.newId("p");
        sketch_.points.push_back({.id = created, .x = u / scale_, .y = v / scale_});
        return created;
    }

    std::string line(const std::string& start, const std::string& end) {
        std::string id = document_.newId("l");
        sketch_.lines.push_back(
            {.id = id, .start = start, .end = end, .construction = construction_});
        return id;
    }

    // Counter-clockwise from `start` to `end` if `ccw`, else the other way round.
    std::string arc(const std::string& center, const std::string& start, const std::string& end,
                    bool ccw) {
        std::string id = document_.newId("a");
        sketch_.arcs.push_back({.id = id,
                                .center = center,
                                .start = ccw ? start : end,
                                .end = ccw ? end : start,
                                .construction = construction_});
        return id;
    }

    std::string circle(const std::string& center, double radius) {
        std::string id = document_.newId("c");
        sketch_.circles.push_back(
            {.id = id, .center = center, .radius = radius / scale_, .construction = construction_});
        return id;
    }

    void constrain(std::string type, std::vector<std::string> entities) {
        sketch_.constraints.push_back(
            {.id = document_.newId("k"), .type = std::move(type), .entities = std::move(entities)});
    }

    // A closed polygon through `corners`, counter-clockwise; returns its lines in order.
    std::vector<std::string> polygon(std::vector<std::tuple<double, double, std::string>> corners) {
        double area = 0.0;
        for (std::size_t i = 0; i < corners.size(); ++i) {
            const auto& [u0, v0, id0] = corners[i];
            const auto& [u1, v1, id1] = corners[(i + 1) % corners.size()];
            area += u0 * v1 - u1 * v0;
        }
        if (area < 0.0) {
            std::reverse(corners.begin() + 1, corners.end());
        }
        std::vector<std::string> points;
        for (const auto& [u, v, id] : corners) {
            points.push_back(point(u, v, id));
        }
        std::vector<std::string> lines;
        for (std::size_t i = 0; i < points.size(); ++i) {
            lines.push_back(line(points[i], points[(i + 1) % points.size()]));
        }
        return lines;
    }

  private:
    Document& document_;
    Sketch& sketch_;
    bool construction_;
    double scale_;  // millimeters per document unit
};

// The four corners of a rectangle with its sides along the axes, from opposite corners.
std::vector<std::tuple<double, double, std::string>> axisRectangle(double u0, double v0,
                                                                   const std::string& id0,
                                                                   double u1, double v1,
                                                                   const std::string& id1) {
    return {{u0, v0, id0}, {u1, v0, {}}, {u1, v1, id1}, {u0, v1, {}}};
}

}  // namespace

SketchTool::SketchTool(SketchToolKind kind, std::string sketch, bool construction)
    : kind_(kind), sketch_(std::move(sketch)), construction_(construction) {}

SketchTool::Spot SketchTool::snap(const Session& session, double u, double v, double radius) const {
    const Document& document = session.document();
    Spot spot{.u = u, .v = v, .point = {}};
    double best = radius;
    for (const PlacedPoint& point : pointsOf(document, sketchOf(document, sketch_))) {
        const double d = distance(u, v, point.u, point.v);
        if (d <= best) {
            best = d;
            spot = {.u = point.u, .v = point.v, .point = point.id};
        }
    }
    return spot;
}

std::optional<std::pair<double, double>> SketchTool::tangentAt(const Session& session,
                                                               const Spot& end) const {
    if (end.point.empty()) {
        return std::nullopt;
    }
    const Document& document = session.document();
    const Sketch& sketch = sketchOf(document, sketch_);
    const auto points = pointsOf(document, sketch);
    const auto at = [&](const std::string& id) -> std::optional<PlacedPoint> {
        for (const PlacedPoint& point : points) {
            if (point.id == id) {
                return point;
            }
        }
        return std::nullopt;
    };
    // The newest curve that ends there decides; the direction leads away from it.
    for (auto line = sketch.lines.rbegin(); line != sketch.lines.rend(); ++line) {
        if (line->start != end.point && line->end != end.point) {
            continue;
        }
        const auto other = at(line->start == end.point ? line->end : line->start);
        if (other) {
            return normalized(end.u - other->u, end.v - other->v);
        }
    }
    for (auto arc = sketch.arcs.rbegin(); arc != sketch.arcs.rend(); ++arc) {
        const auto center = at(arc->center);
        if (!center || (arc->start != end.point && arc->end != end.point)) {
            continue;
        }
        // Counter-clockwise from start to end: leaving the end goes on counter-clockwise,
        // leaving the start goes back the other way.
        return arcDirectionAt(center->u, center->v, end.u, end.v, arc->end == end.point);
    }
    return std::nullopt;
}

std::vector<SketchTool::Spot> SketchTool::placed(double radius) const {
    std::vector<Spot> result = fixed_;
    if (pressed_ && pointer_ &&
        distance(pressed_->u, pressed_->v, pointer_->u, pointer_->v) > radius &&
        (result.empty() ||
         distance(result.back().u, result.back().v, pressed_->u, pressed_->v) > radius)) {
        result.push_back(*pressed_);
    }
    return result;
}

bool SketchTool::pointer(Session& session, PointerEvent event, double u, double v,
                         double snapRadius) {
    lastRadius_ = snapRadius;
    const Spot spot = snap(session, u, v, snapRadius);
    if (kind_ == SketchToolKind::ArcCenter && fixed_.size() == 2) {
        // Follow the pointer round the center, so the arc can go either way and past half.
        const double angle = std::atan2(spot.v - fixed_[0].v, spot.u - fixed_[0].u);
        sweep_ += std::remainder(angle - lastAngle_, 2.0 * std::numbers::pi);
        lastAngle_ = angle;
    }
    pointer_ = spot;
    if (event == PointerEvent::Down) {
        pressed_ = spot;
        return false;
    }
    if (event == PointerEvent::Move) {
        return false;
    }
    if (!pressed_) {
        return click(session, spot, snapRadius);
    }
    const Spot start = *pressed_;
    pressed_.reset();
    if (distance(start.u, start.v, spot.u, spot.v) <= snapRadius) {
        return click(session, spot, snapRadius);
    }
    // A drag. Dragging back from the end of a polyline draws a tangent arc.
    if (kind_ == SketchToolKind::Line && !fixed_.empty() && tangent_ &&
        distance(start.u, start.v, fixed_.back().u, fixed_.back().v) <= snapRadius) {
        const Spot from = fixed_.back();
        const auto arc = tangentArc(from.u, from.v, *tangent_, spot.u, spot.v);
        if (!arc) {
            return click(session, spot, snapRadius);  // straight ahead: a line
        }
        const auto [cu, cv, ccw] = *arc;
        std::string end;
        session.edit(
            [&](Document& document) {
                Sketch& sketch = std::get<Sketch>(document.findFeature(sketch_)->data);
                Builder build(document, sketch, construction_);
                const std::string center = build.point(cu, cv);
                end = build.point(spot.u, spot.v, spot.point);
                build.arc(center, build.point(from.u, from.v, from.point), end, ccw);
            },
            "Draw");
        tangent_ = arcDirectionAt(cu, cv, spot.u, spot.v, ccw);
        fixed_ = {Spot{.u = spot.u, .v = spot.v, .point = end}};
        if (end == chainStart_) {
            cancel();
        }
        return true;
    }
    // Otherwise a drag counts as a click where it started (unless the shape goes on from
    // there) and one where it ended.
    bool changed = false;
    if (fixed_.empty() ||
        distance(fixed_.back().u, fixed_.back().v, start.u, start.v) > snapRadius) {
        changed = click(session, start, snapRadius);
    }
    return click(session, spot, snapRadius) || changed;
}

bool SketchTool::click(Session& session, const Spot& spot, double snapRadius) {
    const auto same = [&](const Spot& a, const Spot& b) {
        return (!a.point.empty() && a.point == b.point) ||
               distance(a.u, a.v, b.u, b.v) <= std::max(snapRadius, kEpsilon);
    };
    const auto edit = [&](const auto& build) {
        session.edit(
            [&](Document& document) {
                Sketch& sketch = std::get<Sketch>(document.findFeature(sketch_)->data);
                Builder builder(document, sketch, construction_);
                build(builder);
            },
            "Draw");
    };
    static_cast<void>(sketchOf(session.document(), sketch_));  // throws if it is gone

    switch (kind_) {
        case SketchToolKind::Point: {
            if (!spot.point.empty()) {
                return false;  // there is a point already
            }
            edit([&](Builder& build) { build.point(spot.u, spot.v); });
            return true;
        }
        case SketchToolKind::Line: {
            if (fixed_.empty()) {
                fixed_ = {spot};
                tangent_.reset();
                chainStart_.clear();
                return false;
            }
            const Spot from = fixed_.back();
            if (same(from, spot)) {
                return false;
            }
            std::string start;
            std::string end;
            edit([&](Builder& build) {
                start = build.point(from.u, from.v, from.point);
                end = build.point(spot.u, spot.v, spot.point);
                build.line(start, end);
            });
            if (chainStart_.empty()) {
                chainStart_ = start;
            }
            tangent_ = normalized(spot.u - from.u, spot.v - from.v);
            fixed_ = {Spot{.u = spot.u, .v = spot.v, .point = end}};
            if (end == chainStart_) {
                cancel();  // the polyline closed
            }
            return true;
        }
        case SketchToolKind::ArcTangent: {
            if (fixed_.empty()) {
                // It starts at the end of a line or arc.
                tangent_ = tangentAt(session, spot);
                if (tangent_) {
                    fixed_ = {spot};
                }
                return false;
            }
            const Spot from = fixed_.front();
            const auto arc = tangentArc(from.u, from.v, *tangent_, spot.u, spot.v);
            if (!arc || same(from, spot)) {
                return false;
            }
            const auto [cu, cv, ccw] = *arc;
            edit([&](Builder& build) {
                build.arc(build.point(cu, cv), from.point, build.point(spot.u, spot.v, spot.point),
                          ccw);
            });
            cancel();
            return true;
        }
        default:
            break;
    }

    // The other tools collect a fixed number of points, then draw.
    if (!fixed_.empty() && same(fixed_.back(), spot)) {
        return false;
    }
    const std::size_t needed = [&] {
        switch (kind_) {
            case SketchToolKind::RectangleThreePoint:
            case SketchToolKind::CircleThreePoint:
            case SketchToolKind::ArcThreePoint:
            case SketchToolKind::ArcCenter:
                return std::size_t{3};
            default:
                return std::size_t{2};
        }
    }();
    if (fixed_.size() + 1 < needed) {
        fixed_.push_back(spot);
        if (kind_ == SketchToolKind::ArcCenter && fixed_.size() == 2) {
            lastAngle_ = std::atan2(spot.v - fixed_[0].v, spot.u - fixed_[0].u);
            sweep_ = 0.0;
        }
        return false;
    }
    const Spot& a = fixed_[0];
    const Spot& b = fixed_.size() > 1 ? fixed_[1] : spot;
    const Spot& c = spot;
    bool drawn = false;
    switch (kind_) {
        case SketchToolKind::RectangleTwoPoint:
        case SketchToolKind::RectangleCenter: {
            const bool centered = kind_ == SketchToolKind::RectangleCenter;
            const double du = std::abs(c.u - a.u);
            const double dv = std::abs(c.v - a.v);
            if (du <= kEpsilon || dv <= kEpsilon) {
                return false;
            }
            const auto corners = centered
                                     ? axisRectangle(a.u - du, a.v - dv, {}, a.u + du, a.v + dv, {})
                                     : axisRectangle(a.u, a.v, a.point, c.u, c.v, c.point);
            edit([&](Builder& build) {
                const auto lines = build.polygon(corners);
                for (std::size_t i = 0; i < lines.size(); ++i) {
                    build.constrain(i % 2 == 0 ? "horizontal" : "vertical", {lines[i]});
                }
            });
            drawn = true;
            break;
        }
        case SketchToolKind::RectangleThreePoint: {
            const double length = distance(a.u, a.v, b.u, b.v);
            const double nu = -(b.v - a.v) / length;
            const double nv = (b.u - a.u) / length;
            const double width = (c.u - a.u) * nu + (c.v - a.v) * nv;
            if (std::abs(width) <= kEpsilon) {
                return false;
            }
            edit([&](Builder& build) {
                const auto lines = build.polygon({{a.u, a.v, a.point},
                                                  {b.u, b.v, b.point},
                                                  {b.u + nu * width, b.v + nv * width, {}},
                                                  {a.u + nu * width, a.v + nv * width, {}}});
                build.constrain("perpendicular", {lines[0], lines[1]});
                build.constrain("parallel", {lines[0], lines[2]});
                build.constrain("parallel", {lines[1], lines[3]});
            });
            drawn = true;
            break;
        }
        case SketchToolKind::CircleCenter: {
            const double radius = distance(a.u, a.v, c.u, c.v);
            edit([&](Builder& build) { build.circle(build.point(a.u, a.v, a.point), radius); });
            drawn = true;
            break;
        }
        case SketchToolKind::CircleTwoPoint: {
            const double radius = distance(a.u, a.v, c.u, c.v) / 2.0;
            edit([&](Builder& build) {
                build.circle(build.point((a.u + c.u) / 2.0, (a.v + c.v) / 2.0), radius);
            });
            drawn = true;
            break;
        }
        case SketchToolKind::CircleThreePoint: {
            const auto circle = circleThrough(a.u, a.v, b.u, b.v, c.u, c.v);
            if (!circle) {
                return false;
            }
            const auto [cu, cv, radius] = *circle;
            edit([&](Builder& build) { build.circle(build.point(cu, cv), radius); });
            drawn = true;
            break;
        }
        case SketchToolKind::ArcThreePoint: {
            const auto circle = circleThrough(a.u, a.v, b.u, b.v, c.u, c.v);
            if (!circle) {
                return false;
            }
            const auto [cu, cv, radius] = *circle;
            const double start = std::atan2(a.v - cv, a.u - cu);
            const bool ccw = ccwAngle(start, std::atan2(c.v - cv, c.u - cu)) <
                             ccwAngle(start, std::atan2(b.v - cv, b.u - cu));
            edit([&](Builder& build) {
                build.arc(build.point(cu, cv), build.point(a.u, a.v, a.point),
                          build.point(b.u, b.v, b.point), ccw);
            });
            drawn = true;
            break;
        }
        case SketchToolKind::ArcCenter: {
            // Center a, start b; the end lies where the pointer points, on the circle.
            const double radius = distance(a.u, a.v, b.u, b.v);
            const double angle = std::atan2(c.v - a.v, c.u - a.u);
            const double eu = a.u + radius * std::cos(angle);
            const double ev = a.v + radius * std::sin(angle);
            if (distance(eu, ev, b.u, b.v) <= kEpsilon) {
                return false;
            }
            const bool onCircle = distance(c.u, c.v, eu, ev) <= std::max(radius, 1.0) * 1e-9;
            const bool ccw = sweep_ != 0.0
                                 ? sweep_ > 0.0
                                 : (b.u - a.u) * (ev - a.v) - (b.v - a.v) * (eu - a.u) > 0.0;
            edit([&](Builder& build) {
                build.arc(build.point(a.u, a.v, a.point), build.point(b.u, b.v, b.point),
                          build.point(eu, ev, onCircle ? c.point : std::string{}), ccw);
            });
            drawn = true;
            break;
        }
        default:
            break;
    }
    fixed_.clear();
    return drawn;
}

bool SketchTool::cancel() {
    const bool busy = !fixed_.empty() || pressed_.has_value();
    fixed_.clear();
    pressed_.reset();
    tangent_.reset();
    chainStart_.clear();
    sweep_ = 0.0;
    return busy;
}

Mesh SketchTool::preview(const Session& session, double deflection) const {
    const auto& sketches = session.regeneration().model.sketches;
    const auto sketch = sketches.find(sketch_);
    if (!pointer_ || sketch == sketches.end()) {
        return {};
    }
    const SketchFrame& frame = sketch->second->frame;
    const Spot& p = *pointer_;
    std::vector<Spot> spots = placed(lastRadius_);
    std::vector<ProfileSegment> curves;
    const auto rectangle = [&](const std::vector<std::tuple<double, double, std::string>>& c) {
        for (std::size_t i = 0; i < c.size(); ++i) {
            const auto& [u0, v0, id0] = c[i];
            const auto& [u1, v1, id1] = c[(i + 1) % c.size()];
            curves.push_back(lineSegment(u0, v0, u1, v1));
        }
    };
    const bool draggingArc =
        kind_ == SketchToolKind::Line && pressed_ && tangent_ && !fixed_.empty() &&
        distance(pressed_->u, pressed_->v, fixed_.back().u, fixed_.back().v) <= lastRadius_;
    if (draggingArc) {
        spots = fixed_;
    }
    if (!spots.empty()) {
        const Spot& a = spots[0];
        switch (kind_) {
            case SketchToolKind::Line:
            case SketchToolKind::ArcTangent: {
                const Spot& from = spots.back();
                if ((draggingArc || kind_ == SketchToolKind::ArcTangent) && tangent_) {
                    if (const auto arc = tangentArc(from.u, from.v, *tangent_, p.u, p.v)) {
                        const auto [cu, cv, ccw] = *arc;
                        curves.push_back(arcSegment(cu, cv, from.u, from.v, p.u, p.v, ccw));
                        break;
                    }
                }
                curves.push_back(lineSegment(from.u, from.v, p.u, p.v));
                break;
            }
            case SketchToolKind::RectangleTwoPoint:
                rectangle(axisRectangle(a.u, a.v, {}, p.u, p.v, {}));
                break;
            case SketchToolKind::RectangleCenter: {
                const double du = std::abs(p.u - a.u);
                const double dv = std::abs(p.v - a.v);
                rectangle(axisRectangle(a.u - du, a.v - dv, {}, a.u + du, a.v + dv, {}));
                break;
            }
            case SketchToolKind::RectangleThreePoint: {
                if (spots.size() == 1) {
                    curves.push_back(lineSegment(a.u, a.v, p.u, p.v));
                    break;
                }
                const Spot& b = spots[1];
                const double length = std::max(distance(a.u, a.v, b.u, b.v), kEpsilon);
                const double nu = -(b.v - a.v) / length;
                const double nv = (b.u - a.u) / length;
                const double width = (p.u - a.u) * nu + (p.v - a.v) * nv;
                rectangle({{a.u, a.v, {}},
                           {b.u, b.v, {}},
                           {b.u + nu * width, b.v + nv * width, {}},
                           {a.u + nu * width, a.v + nv * width, {}}});
                break;
            }
            case SketchToolKind::CircleCenter:
                curves.push_back(circleSegment(a.u, a.v, distance(a.u, a.v, p.u, p.v)));
                break;
            case SketchToolKind::CircleTwoPoint:
                curves.push_back(circleSegment((a.u + p.u) / 2.0, (a.v + p.v) / 2.0,
                                               distance(a.u, a.v, p.u, p.v) / 2.0));
                break;
            case SketchToolKind::CircleThreePoint:
            case SketchToolKind::ArcThreePoint: {
                if (spots.size() == 1) {
                    curves.push_back(lineSegment(a.u, a.v, p.u, p.v));
                    break;
                }
                const Spot& b = spots[1];
                const auto circle = circleThrough(a.u, a.v, b.u, b.v, p.u, p.v);
                if (!circle) {
                    curves.push_back(lineSegment(a.u, a.v, b.u, b.v));
                    break;
                }
                const auto [cu, cv, radius] = *circle;
                if (kind_ == SketchToolKind::CircleThreePoint) {
                    curves.push_back(circleSegment(cu, cv, radius));
                } else {
                    const double start = std::atan2(a.v - cv, a.u - cu);
                    const bool ccw = ccwAngle(start, std::atan2(p.v - cv, p.u - cu)) <
                                     ccwAngle(start, std::atan2(b.v - cv, b.u - cu));
                    curves.push_back(arcSegment(cu, cv, a.u, a.v, b.u, b.v, ccw));
                }
                break;
            }
            case SketchToolKind::ArcCenter: {
                if (spots.size() == 1) {
                    curves.push_back(lineSegment(a.u, a.v, p.u, p.v));
                    break;
                }
                const Spot& b = spots[1];
                const double radius = distance(a.u, a.v, b.u, b.v);
                const double angle = std::atan2(p.v - a.v, p.u - a.u);
                const double eu = a.u + radius * std::cos(angle);
                const double ev = a.v + radius * std::sin(angle);
                const bool ccw = sweep_ != 0.0
                                     ? sweep_ > 0.0
                                     : (b.u - a.u) * (ev - a.v) - (b.v - a.v) * (eu - a.u) > 0.0;
                curves.push_back(arcSegment(a.u, a.v, b.u, b.v, eu, ev, ccw));
                break;
            }
            case SketchToolKind::Point:
                break;
        }
    }
    std::vector<Vec3> points;
    for (const Spot& spot : spots) {
        points.push_back(frame.at(spot.u, spot.v));
    }
    points.push_back(frame.at(p.u, p.v));
    return curvesMesh(frame, curves, points, deflection);
}

}  // namespace facetry::core
