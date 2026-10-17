#include "facetry/core/sketch_tool.hpp"

#include "facetry/core/error.hpp"
#include "facetry/core/regeneration.hpp"

#include "sketch_geometry.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <numbers>
#include <set>
#include <tuple>

namespace facetry::core {
namespace {

using namespace sketching;

// Adds entities to a sketch, with IDs from the document and values in its units.
// A tool's preview: curves and points, and guides as edges of their own from kPreviewGuideId on.
Mesh previewMesh(const SketchFrame& frame, const std::vector<ProfileSegment>& curves,
                 const std::vector<Vec3>& points, const std::vector<ProfileSegment>& guides,
                 double deflection) {
    Mesh mesh = curvesMesh(frame, curves, points, deflection);
    const Mesh guideMesh = curvesMesh(frame, guides, {}, deflection);
    const auto offset = static_cast<std::uint32_t>(mesh.edgePoints.size() / 3);
    mesh.edgePoints.insert(mesh.edgePoints.end(), guideMesh.edgePoints.begin(),
                           guideMesh.edgePoints.end());
    for (std::size_t i = 0; i < guideMesh.edgeIds.size(); ++i) {
        mesh.edgeOffsets.push_back(offset + guideMesh.edgeOffsets[i + 1]);
        mesh.edgeIds.push_back(kPreviewGuideId + static_cast<std::uint32_t>(i));
    }
    return mesh;
}

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

    // The point a spot snapped to, or a new one with the constraints of its snap.
    std::string point(const SketchTool::Spot& spot) {
        if (!spot.point.empty()) {
            return spot.point;
        }
        std::string created = point(spot.u, spot.v);
        if (spot.kind == "midpoint" && !spot.curves.empty()) {
            constrain("midpoint", {created, spot.curves.front()});
        } else {
            for (const std::string& curve : spot.curves) {
                constrain("coincident", {created, curve});  // on a curve, or on two
            }
        }
        return created;
    }

    // The constraints the line tool inferred for `line`.
    void infer(const std::string& line, const std::vector<SketchTool::Inference>& inferences) {
        for (const SketchTool::Inference& inference : inferences) {
            if (inference.other.empty()) {
                constrain(inference.type, {line});
            } else if (inference.type == "tangent") {
                constrain("tangent", {inference.other, line});
            } else {
                constrain(inference.type, {line, inference.other});
            }
        }
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

    // A closed polygon through `corners`, counter-clockwise; returns its lines in order, each with
    // the corners it runs between.
    std::vector<std::tuple<std::string, SketchTool::Spot, SketchTool::Spot>> polygon(
        std::vector<SketchTool::Spot> corners) {
        double area = 0.0;
        for (std::size_t i = 0; i < corners.size(); ++i) {
            const SketchTool::Spot& a = corners[i];
            const SketchTool::Spot& b = corners[(i + 1) % corners.size()];
            area += a.u * b.v - b.u * a.v;
        }
        if (area < 0.0) {
            std::reverse(corners.begin() + 1, corners.end());
        }
        std::vector<std::string> points;
        for (const SketchTool::Spot& corner : corners) {
            points.push_back(point(corner));
        }
        std::vector<std::tuple<std::string, SketchTool::Spot, SketchTool::Spot>> lines;
        for (std::size_t i = 0; i < points.size(); ++i) {
            const std::size_t next = (i + 1) % points.size();
            lines.emplace_back(line(points[i], points[next]), corners[i], corners[next]);
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
std::vector<SketchTool::Spot> axisRectangle(const SketchTool::Spot& a, const SketchTool::Spot& c) {
    return {a, {.u = c.u, .v = a.v}, c, {.u = a.u, .v = c.v}};
}

// The corners of a rectangle with one side from a to b and the given width.
std::vector<SketchTool::Spot> sideRectangle(const SketchTool::Spot& a, const SketchTool::Spot& b,
                                            double nu, double nv, double width) {
    return {a,
            b,
            {.u = b.u + nu * width, .v = b.v + nv * width},
            {.u = a.u + nu * width, .v = a.v + nv * width}};
}

}  // namespace

SketchTool::SketchTool(SketchToolKind kind, std::string sketch, bool construction)
    : kind_(kind), sketch_(std::move(sketch)), construction_(construction) {}

SketchTool::Spot SketchTool::snap(const Session& session, double u, double v, double radius) const {
    const Document& document = session.document();
    const SnapGeometry geometry = snapGeometry(document, sketchOf(document, sketch_));

    // Geometry first, by rank: points, then special points, then anywhere on a curve; the
    // nearest within the radius wins within a rank.
    Spot best{.u = u, .v = v};
    int bestRank = 3;
    double bestDistance = radius;
    const auto consider = [&](int rank, double su, double sv, std::string kind,
                              std::vector<std::string> curves, std::string point = {}) {
        const double d = distance(u, v, su, sv);
        if (d > radius || rank > bestRank || (rank == bestRank && d >= bestDistance)) {
            return;
        }
        bestRank = rank;
        bestDistance = d;
        best = {.u = su,
                .v = sv,
                .point = std::move(point),
                .kind = std::move(kind),
                .curves = std::move(curves)};
    };
    for (const PlacedPoint& point : geometry.points) {
        consider(0, point.u, point.v, geometry.centers.contains(point.id) ? "center" : "point", {},
                 point.id);
    }
    for (const SnapLine& line : geometry.lines) {
        consider(1, (line.u0 + line.u1) / 2, (line.v0 + line.v1) / 2, "midpoint", {line.id});
    }
    for (const SnapCircle& circle : geometry.circles) {
        for (int k = 0; k < 4; ++k) {
            const double angle = k * std::numbers::pi / 2;
            if (circle.covers(angle)) {
                consider(1, circle.cu + circle.radius * std::cos(angle),
                         circle.cv + circle.radius * std::sin(angle), "quadrant", {circle.id});
            }
        }
    }
    consider(1, 0.0, 0.0, "origin", {});
    // Curves that pass near the pointer: where it lies on them, and where they meet.
    std::vector<const SnapLine*> nearLines;
    std::vector<const SnapCircle*> nearCircles;
    for (const SnapLine& line : geometry.lines) {
        const auto [qu, qv] = nearestOn(line, u, v);
        if (distance(u, v, qu, qv) <= radius) {
            nearLines.push_back(&line);
            consider(2, qu, qv, "onCurve", {line.id});
        }
    }
    for (const SnapCircle& circle : geometry.circles) {
        if (const auto q = nearestOn(circle, u, v);
            q && distance(u, v, q->first, q->second) <= radius) {
            nearCircles.push_back(&circle);
            consider(2, q->first, q->second, "onCurve", {circle.id});
        }
    }
    const auto meet = [&](const auto& points, const std::string& a, const std::string& b) {
        for (const auto& [mu, mv] : points) {
            consider(1, mu, mv, "intersection", {a, b});
        }
    };
    for (std::size_t i = 0; i < nearLines.size(); ++i) {
        for (std::size_t j = i + 1; j < nearLines.size(); ++j) {
            meet(intersections(*nearLines[i], *nearLines[j]), nearLines[i]->id, nearLines[j]->id);
        }
        for (const SnapCircle* circle : nearCircles) {
            meet(intersections(*nearLines[i], *circle), nearLines[i]->id, circle->id);
        }
    }
    for (std::size_t i = 0; i < nearCircles.size(); ++i) {
        for (std::size_t j = i + 1; j < nearCircles.size(); ++j) {
            meet(intersections(*nearCircles[i], *nearCircles[j]), nearCircles[i]->id,
                 nearCircles[j]->id);
        }
    }
    if (bestRank < 3) {
        return best;
    }

    // A free position. The line tool infers a direction from where the line starts:
    // horizontal or vertical first, then tangent or perpendicular to the curve it leaves, then
    // parallel or perpendicular to lines nearby.
    Spot spot{.u = u, .v = v};
    std::optional<std::pair<double, double>> ray;  // the inferred direction, from the start
    if (kind_ == SketchToolKind::Line && !fixed_.empty()) {
        const Spot& from = fixed_.back();
        struct Direction {
            Vector d;
            std::string type;
            std::string other;
            int rank;
        };
        std::vector<Direction> directions{{{1.0, 0.0}, "horizontal", {}, 0},
                                          {{0.0, 1.0}, "vertical", {}, 0}};
        std::optional<Tangent> leaving;
        if (tangent_ && !lastCurve_.empty()) {
            leaving = Tangent{*tangent_, lastCurve_, lastCurveArc_};
        } else if (fixed_.size() == 1) {
            leaving = tangentAt(session, from);  // starting at the end of an existing curve
        }
        if (leaving) {
            const Vector d = leaving->direction;
            if (leaving->arc) {
                directions.push_back({d, "tangent", leaving->curve, 1});
            } else {
                directions.push_back({{-d.second, d.first}, "perpendicular", leaving->curve, 1});
            }
        }
        for (const SnapLine& line : geometry.lines) {
            if ((leaving && line.id == leaving->curve) ||
                distance(u, v, nearestOn(line, u, v).first, nearestOn(line, u, v).second) >
                    4 * radius) {
                continue;
            }
            const Vector d = normalized(line.u1 - line.u0, line.v1 - line.v0);
            directions.push_back({d, "parallel", line.id, 2});
            directions.push_back({{-d.second, d.first}, "perpendicular", line.id, 2});
        }
        const Direction* chosen = nullptr;
        double chosenAway = 0.0;
        for (const Direction& direction : directions) {
            const double wu = u - from.u;
            const double wv = v - from.v;
            const double along = wu * direction.d.first + wv * direction.d.second;
            const double away = std::abs(wu * direction.d.second - wv * direction.d.first);
            const bool forward = direction.type != "tangent" || along > 0.0;
            if (away > radius || !forward || std::abs(along) < radius) {
                continue;
            }
            if (chosen == nullptr || direction.rank < chosen->rank ||
                (direction.rank == chosen->rank && away < chosenAway)) {
                chosen = &direction;
                chosenAway = away;
            }
        }
        if (chosen != nullptr) {
            const double along = (u - from.u) * chosen->d.first + (v - from.v) * chosen->d.second;
            spot.u = from.u + along * chosen->d.first;
            spot.v = from.v + along * chosen->d.second;
            spot.inferences.push_back({chosen->type, chosen->other});
            spot.guides.push_back({{from.u, from.v}, {spot.u, spot.v}});
            ray = chosen->d;
        }
    }

    // Alignment with existing points: guides through them, along the axes.
    const PlacedPoint* across = nullptr;  // the same u: a vertical guide
    const PlacedPoint* level = nullptr;   // the same v: a horizontal guide
    for (const PlacedPoint& point : geometry.points) {
        if (!fixed_.empty() && point.id == fixed_.back().point && kind_ == SketchToolKind::Line) {
            continue;  // that is horizontal or vertical inference
        }
        if (std::abs(point.u - spot.u) <= radius &&
            (across == nullptr || std::abs(point.u - spot.u) < std::abs(across->u - spot.u))) {
            across = &point;
        }
        if (std::abs(point.v - spot.v) <= radius &&
            (level == nullptr || std::abs(point.v - spot.v) < std::abs(level->v - spot.v))) {
            level = &point;
        }
    }
    const auto align = [&](const PlacedPoint& point, bool vertical) {
        double au = spot.u;
        double av = spot.v;
        if (ray) {
            // Slide along the inferred direction to the guide.
            const Spot& from = fixed_.back();
            const double step = vertical ? ray->first : ray->second;
            if (std::abs(step) < kEpsilon) {
                return;
            }
            const double t = ((vertical ? point.u - from.u : point.v - from.v)) / step;
            au = from.u + t * ray->first;
            av = from.v + t * ray->second;
            if (distance(au, av, spot.u, spot.v) > radius) {
                return;
            }
        } else if (vertical) {
            au = point.u;
        } else {
            av = point.v;
        }
        spot.u = au;
        spot.v = av;
        spot.guides.push_back({{point.u, point.v}, {spot.u, spot.v}});
    };
    if (across != nullptr) {
        align(*across, true);
    }
    if (level != nullptr && (!ray || across == nullptr)) {
        align(*level, false);
    }

    // The grid, when nothing else applies.
    if (grid_ > 0.0 && spot.inferences.empty() && spot.guides.empty()) {
        spot.u = std::round(spot.u / grid_) * grid_;
        spot.v = std::round(spot.v / grid_) * grid_;
        spot.kind = "grid";
    }
    return spot;
}

std::optional<SketchTool::Tangent> SketchTool::tangentAt(const Session& session,
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
            return Tangent{normalized(end.u - other->u, end.v - other->v), line->id, false};
        }
    }
    for (auto arc = sketch.arcs.rbegin(); arc != sketch.arcs.rend(); ++arc) {
        const auto center = at(arc->center);
        if (!center || (arc->start != end.point && arc->end != end.point)) {
            continue;
        }
        // Counter-clockwise from start to end: leaving the end goes on counter-clockwise,
        // leaving the start goes back the other way.
        return Tangent{arcDirectionAt(center->u, center->v, end.u, end.v, arc->end == end.point),
                       arc->id, true};
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
    if (kind_ == SketchToolKind::Trim || kind_ == SketchToolKind::Extend) {
        return trimOrExtend(session, event, u, v, snapRadius);
    }
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
        radius_ = filletRadius(session);
        return false;
    }
    if (event == PointerEvent::Move) {
        radius_ = filletRadius(session);
        return false;
    }
    radius_ = 0.0;
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
        std::string arcId;
        session.edit(
            [&](Document& document) {
                Sketch& sketch = std::get<Sketch>(document.findFeature(sketch_)->data);
                Builder build(document, sketch, construction_);
                const std::string center = build.point(cu, cv);
                end = build.point(spot);
                arcId = build.arc(center, build.point(from), end, ccw);
                if (!lastCurve_.empty()) {
                    build.constrain("tangent", {lastCurve_, arcId});
                }
            },
            "Draw");
        tangent_ = arcDirectionAt(cu, cv, spot.u, spot.v, ccw);
        lastCurve_ = arcId;
        lastCurveArc_ = true;
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
    if (editing()) {
        return editClick(session, spot, snapRadius);
    }

    switch (kind_) {
        case SketchToolKind::Point: {
            if (!spot.point.empty()) {
                return false;  // there is a point already
            }
            edit([&](Builder& build) { build.point(spot); });
            return true;
        }
        case SketchToolKind::Line: {
            if (fixed_.empty()) {
                fixed_ = {spot};
                tangent_.reset();
                chainStart_.clear();
                lastCurve_.clear();
                return false;
            }
            const Spot from = fixed_.back();
            if (same(from, spot)) {
                return false;
            }
            std::string start;
            std::string end;
            std::string line;
            edit([&](Builder& build) {
                start = build.point(from);
                end = build.point(spot);
                line = build.line(start, end);
                build.infer(line, spot.inferences);
            });
            if (chainStart_.empty()) {
                chainStart_ = start;
            }
            tangent_ = normalized(spot.u - from.u, spot.v - from.v);
            lastCurve_ = line;
            lastCurveArc_ = false;
            fixed_ = {Spot{.u = spot.u, .v = spot.v, .point = end}};
            if (end == chainStart_) {
                cancel();  // the polyline closed
            }
            return true;
        }
        case SketchToolKind::ArcTangent: {
            if (fixed_.empty()) {
                // It starts at the end of a line or arc.
                const auto leaving = tangentAt(session, spot);
                if (leaving) {
                    tangent_ = leaving->direction;
                    lastCurve_ = leaving->curve;
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
                const std::string arc =
                    build.arc(build.point(cu, cv), from.point, build.point(spot), ccw);
                build.constrain("tangent", {lastCurve_, arc});
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
            const auto corners = centered ? axisRectangle({.u = a.u - du, .v = a.v - dv},
                                                          {.u = a.u + du, .v = a.v + dv})
                                          : axisRectangle(a, c);
            edit([&](Builder& build) {
                const auto lines = build.polygon(corners);
                // The polygon may run the other way round: a side is horizontal if its line is.
                for (const auto& [line, from, to] : lines) {
                    build.constrain(std::abs(from.v - to.v) <= kEpsilon ? "horizontal" : "vertical",
                                    {line});
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
                const auto lines = build.polygon(sideRectangle(a, b, nu, nv, width));
                build.constrain("perpendicular", {std::get<0>(lines[0]), std::get<0>(lines[1])});
                build.constrain("parallel", {std::get<0>(lines[0]), std::get<0>(lines[2])});
                build.constrain("parallel", {std::get<0>(lines[1]), std::get<0>(lines[3])});
            });
            drawn = true;
            break;
        }
        case SketchToolKind::CircleCenter: {
            const double radius = distance(a.u, a.v, c.u, c.v);
            edit([&](Builder& build) { build.circle(build.point(a), radius); });
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
                build.arc(build.point(cu, cv), build.point(a), build.point(b), ccw);
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
                build.arc(build.point(a), build.point(b),
                          onCircle ? build.point(c) : build.point(eu, ev), ccw);
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
    lastCurve_.clear();
    lastCurveArc_ = false;
    sweep_ = 0.0;
    radius_ = 0.0;
    if (pickedSelection_) {
        selection_.clear();
        pickedSelection_ = false;
    }
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
    std::vector<ProfileSegment> guides;
    for (const auto& [from, to] : p.guides) {
        guides.push_back(lineSegment(from.first, from.second, to.first, to.second));
    }
    if (editing()) {
        EditPreview edit = editPreview(session);
        std::vector<Vec3> points;
        for (const auto& [u, v] : edit.points) {
            points.push_back(frame.at(u, v));
        }
        edit.guides.insert(edit.guides.end(), guides.begin(), guides.end());
        return previewMesh(frame, edit.curves, points, edit.guides, deflection);
    }
    std::vector<Spot> spots = placed(lastRadius_);
    std::vector<ProfileSegment> curves;
    const auto rectangle = [&](const std::vector<Spot>& c) {
        for (std::size_t i = 0; i < c.size(); ++i) {
            const Spot& from = c[i];
            const Spot& to = c[(i + 1) % c.size()];
            curves.push_back(lineSegment(from.u, from.v, to.u, to.v));
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
                rectangle(axisRectangle(a, p));
                break;
            case SketchToolKind::RectangleCenter: {
                const double du = std::abs(p.u - a.u);
                const double dv = std::abs(p.v - a.v);
                rectangle(
                    axisRectangle({.u = a.u - du, .v = a.v - dv}, {.u = a.u + du, .v = a.v + dv}));
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
                rectangle(sideRectangle(a, b, nu, nv, width));
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
            default:  // points, and the editing tools above
                break;
        }
    }
    std::vector<Vec3> points;
    for (const Spot& spot : spots) {
        points.push_back(frame.at(spot.u, spot.v));
    }
    points.push_back(frame.at(p.u, p.v));
    return previewMesh(frame, curves, points, guides, deflection);
}

SnapFeedback SketchTool::feedback() const {
    SnapFeedback result;
    if (pointer_) {
        result.snap = pointer_->kind;
        for (const Inference& inference : pointer_->inferences) {
            result.inferences.push_back(inference.type);
        }
    }
    result.radius = radius_;
    return result;
}

}  // namespace facetry::core
