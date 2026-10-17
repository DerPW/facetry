#include "facetry/core/sketch_edit.hpp"

#include "facetry/core/error.hpp"
#include "facetry/core/regeneration.hpp"

#include "sketch_edit_plan.hpp"
#include "sketch_geometry.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <set>

namespace facetry::core {
namespace sketching {
namespace {

// Positions closer than this, in millimeters, are the same place.
constexpr double kSamePlace = 1e-6;

// Cuts along a curve: merged where curves meet at one place, sorted by parameter.
class Cuts {
  public:
    void add(double at, double u, double v, const std::string& curve) {
        for (Cut& cut : cuts_) {
            if (distance(cut.u, cut.v, u, v) <= kSamePlace) {
                cut.curves.push_back(curve);
                return;
            }
        }
        cuts_.push_back({.at = at, .u = u, .v = v, .curves = {curve}});
    }

    [[nodiscard]] std::vector<Cut> sorted() const {
        std::vector<Cut> result = cuts_;
        std::sort(result.begin(), result.end(),
                  [](const Cut& a, const Cut& b) { return a.at < b.at; });
        return result;
    }

  private:
    std::vector<Cut> cuts_;
};

// Where the other curves of the sketch meet a line or circle, apart from its ends: a line's
// parameter is 0 to 1, an arc's the angle from its start, a circle's the angle from the u axis.
template <typename Curve, typename Parameter>
std::vector<Cut> cutsOf(const SnapGeometry& geometry, const Curve& curve,
                        const Parameter& parameter) {
    Cuts cuts;
    for (const SnapLine& other : geometry.lines) {
        if (other.id != curve.id) {
            for (const auto& [u, v] : intersections(other, curve)) {
                cuts.add(parameter(u, v), u, v, other.id);
            }
        }
    }
    for (const SnapCircle& other : geometry.circles) {
        if (other.id != curve.id) {
            for (const auto& [u, v] : intersections(curve, other)) {
                cuts.add(parameter(u, v), u, v, other.id);
            }
        }
    }
    return cuts.sorted();
}

// The cuts before and after `at`.
void around(const std::vector<Cut>& cuts, double at, TrimPlan& plan) {
    for (const Cut& cut : cuts) {
        if (cut.at < at) {
            plan.from = cut;
        } else if (!plan.to) {
            plan.to = cut;
        }
    }
}

}  // namespace

std::optional<TrimPlan> planTrim(const Document& document, const Sketch& sketch, double u, double v,
                                 double radius) {
    const SnapGeometry geometry = snapGeometry(document, sketch);
    const NearestCurve nearest = nearestCurve(geometry, u, v, radius);
    const SnapLine* line = nearest.line;
    const SnapCircle* circle = nearest.circle;
    if (line == nullptr && circle == nullptr) {
        return std::nullopt;
    }
    TrimPlan plan;
    if (line != nullptr) {
        const double du = line->u1 - line->u0;
        const double dv = line->v1 - line->v0;
        const double length = std::hypot(du, dv);
        const auto parameter = [&](double pu, double pv) {
            return ((pu - line->u0) * du + (pv - line->v0) * dv) / (length * length);
        };
        std::vector<Cut> cuts = cutsOf(geometry, *line, parameter);
        // Curves that only touch the line's ends do not cut it.
        const double end = kSamePlace / length;
        std::erase_if(cuts, [&](const Cut& cut) { return cut.at <= end || cut.at >= 1.0 - end; });
        around(cuts, parameter(u, v), plan);
        plan.curve = line->id;
        plan.removed =
            lineSegment(plan.from ? plan.from->u : line->u0, plan.from ? plan.from->v : line->v0,
                        plan.to ? plan.to->u : line->u1, plan.to ? plan.to->v : line->v1);
        return plan;
    }

    const auto pointAt = [&](double angle) {
        return std::pair{circle->cu + circle->radius * std::cos(angle),
                         circle->cv + circle->radius * std::sin(angle)};
    };
    const double base = circle->arc ? circle->start : 0.0;
    const auto parameter = [&](double pu, double pv) {
        return ccwAngle(base, std::atan2(pv - circle->cv, pu - circle->cu));
    };
    std::vector<Cut> cuts = cutsOf(geometry, *circle, parameter);
    const double at = parameter(u, v);
    plan.curve = circle->id;
    if (circle->arc) {
        const double end = kSamePlace / circle->radius;
        std::erase_if(
            cuts, [&](const Cut& cut) { return cut.at <= end || cut.at >= circle->sweep - end; });
        around(cuts, at, plan);
        const auto [u0, v0] = pointAt(base + (plan.from ? plan.from->at : 0.0));
        const auto [u1, v1] = pointAt(base + (plan.to ? plan.to->at : circle->sweep));
        plan.removed = arcSegment(circle->cu, circle->cv, u0, v0, u1, v1, /*ccw=*/true);
        return plan;
    }
    // A circle needs two cuts to keep an arc; with fewer it goes as a whole. The cuts around `at`
    // wrap round past the u axis.
    if (cuts.size() < 2) {
        plan.removed = circleSegment(circle->cu, circle->cv, circle->radius);
        return plan;
    }
    around(cuts, at, plan);
    if (!plan.from) {
        plan.from = cuts.back();
    }
    if (!plan.to) {
        plan.to = cuts.front();
    }
    plan.removed = arcSegment(circle->cu, circle->cv, plan.from->u, plan.from->v, plan.to->u,
                              plan.to->v, /*ccw=*/true);
    return plan;
}

std::optional<ExtendPlan> planExtend(const Document& document, const Sketch& sketch, double u,
                                     double v, double radius) {
    const SnapGeometry geometry = snapGeometry(document, sketch);
    const SnapLine* line = nearestCurve(geometry, u, v, radius, /*linesOnly=*/true).line;
    if (line == nullptr) {
        return std::nullopt;
    }
    const bool atStart = distance(u, v, line->u0, line->v0) < distance(u, v, line->u1, line->v1);
    const double ou = atStart ? line->u0 : line->u1;
    const double ov = atStart ? line->v0 : line->v1;
    const Vector d = atStart ? normalized(line->u0 - line->u1, line->v0 - line->v1)
                             : normalized(line->u1 - line->u0, line->v1 - line->v0);
    std::optional<Cut> hit;
    double nearest = std::numeric_limits<double>::infinity();
    const auto consider = [&](double s, const std::string& curve) {
        if (s > kSamePlace && s < nearest) {
            nearest = s;
            hit = Cut{.at = s, .u = ou + s * d.first, .v = ov + s * d.second, .curves = {curve}};
        }
    };
    for (const SnapLine& other : geometry.lines) {
        if (other.id == line->id) {
            continue;
        }
        const double eu = other.u1 - other.u0;
        const double ev = other.v1 - other.v0;
        const double cross = d.first * ev - d.second * eu;
        if (std::abs(cross) < kEpsilon) {
            continue;
        }
        const double s = ((other.u0 - ou) * ev - (other.v0 - ov) * eu) / cross;
        const double t = ((other.u0 - ou) * d.second - (other.v0 - ov) * d.first) / cross;
        if (t >= -1e-9 && t <= 1 + 1e-9) {
            consider(s, other.id);
        }
    }
    for (const SnapCircle& other : geometry.circles) {
        const double fu = ou - other.cu;
        const double fv = ov - other.cv;
        const double b = fu * d.first + fv * d.second;
        const double c = fu * fu + fv * fv - other.radius * other.radius;
        const double discriminant = b * b - c;
        if (discriminant < 0) {
            continue;
        }
        for (const double sign : {-1.0, 1.0}) {
            const double s = -b + sign * std::sqrt(discriminant);
            const double pu = ou + s * d.first;
            const double pv = ov + s * d.second;
            if (other.covers(std::atan2(pv - other.cv, pu - other.cu))) {
                consider(s, other.id);
            }
        }
    }
    if (!hit) {
        return std::nullopt;
    }
    return ExtendPlan{.line = line->id,
                      .atStart = atStart,
                      .hit = *hit,
                      .added = lineSegment(ou, ov, hit->u, hit->v)};
}

std::optional<FilletPlan> planFillet(const Document& document, const Sketch& sketch,
                                     const std::string& point, double setback) {
    const auto points = pointsOf(document, sketch);
    const auto at = [&](const std::string& id) -> std::optional<std::pair<double, double>> {
        for (const PlacedPoint& p : points) {
            if (p.id == id) {
                return std::pair{p.u, p.v};
            }
        }
        return std::nullopt;
    };
    // Exactly two lines end at the point, and nothing else uses it.
    std::vector<const SketchLine*> lines;
    for (const SketchLine& line : sketch.lines) {
        if (line.start == point || line.end == point) {
            lines.push_back(&line);
        }
    }
    const bool other =
        std::any_of(sketch.arcs.begin(), sketch.arcs.end(),
                    [&](const SketchArc& arc) {
                        return arc.center == point || arc.start == point || arc.end == point;
                    }) ||
        std::any_of(sketch.circles.begin(), sketch.circles.end(),
                    [&](const SketchCircle& circle) { return circle.center == point; });
    const auto corner = at(point);
    if (lines.size() != 2 || other || !corner) {
        return std::nullopt;
    }
    const auto farEnd = [&](const SketchLine& line) {
        return at(line.start == point ? line.end : line.start);
    };
    const auto a = farEnd(*lines[0]);
    const auto b = farEnd(*lines[1]);
    if (!a || !b) {
        return std::nullopt;
    }
    const auto [pu, pv] = *corner;
    const double la = distance(pu, pv, a->first, a->second);
    const double lb = distance(pu, pv, b->first, b->second);
    const Vector d1 = normalized(a->first - pu, a->second - pv);
    const Vector d2 = normalized(b->first - pu, b->second - pv);
    const double cosine = d1.first * d2.first + d1.second * d2.second;
    if (cosine > 1.0 - 1e-9 || cosine < -1.0 + 1e-9 || setback <= kEpsilon ||
        setback >= la - kEpsilon || setback >= lb - kEpsilon) {
        return std::nullopt;
    }
    const double half = std::acos(cosine) / 2.0;
    const Vector w = normalized(d1.first + d2.first, d1.second + d2.second);
    const double toCenter = setback / std::cos(half);
    FilletPlan plan{.point = point,
                    .first = lines[0]->id,
                    .second = lines[1]->id,
                    .u1 = pu + d1.first * setback,
                    .v1 = pv + d1.second * setback,
                    .u2 = pu + d2.first * setback,
                    .v2 = pv + d2.second * setback,
                    .cu = pu + w.first * toCenter,
                    .cv = pv + w.second * toCenter};
    plan.ccw =
        (plan.u1 - plan.cu) * (plan.v2 - plan.cv) - (plan.v1 - plan.cv) * (plan.u2 - plan.cu) > 0.0;
    plan.arc = arcSegment(plan.cu, plan.cv, plan.u1, plan.v1, plan.u2, plan.v2, plan.ccw);
    return plan;
}

std::vector<ProfileSegment> movedCurves(const Document& document, const Sketch& sketch,
                                        const std::vector<std::string>& entities, double du,
                                        double dv) {
    const SnapGeometry geometry = snapGeometry(document, sketch);
    const std::set<std::string> chosen(entities.begin(), entities.end());
    std::vector<ProfileSegment> result;
    for (const SnapLine& line : geometry.lines) {
        if (chosen.contains(line.id)) {
            result.push_back(lineSegment(line.u0 + du, line.v0 + dv, line.u1 + du, line.v1 + dv));
        }
    }
    for (const SnapCircle& circle : geometry.circles) {
        if (!chosen.contains(circle.id)) {
            continue;
        }
        if (!circle.arc) {
            result.push_back(circleSegment(circle.cu + du, circle.cv + dv, circle.radius));
            continue;
        }
        const double end = circle.start + circle.sweep;
        result.push_back(arcSegment(circle.cu + du, circle.cv + dv,
                                    circle.cu + du + circle.radius * std::cos(circle.start),
                                    circle.cv + dv + circle.radius * std::sin(circle.start),
                                    circle.cu + du + circle.radius * std::cos(end),
                                    circle.cv + dv + circle.radius * std::sin(end), true));
    }
    return result;
}

}  // namespace sketching

namespace {

using namespace sketching;

Sketch& sketchIn(Document& document, const std::string& id) {
    Feature* feature = document.findFeature(id);
    if (feature == nullptr || !std::holds_alternative<Sketch>(feature->data)) {
        throw Error(ErrorCode::InvalidArgument, "The sketch " + id + " does not exist");
    }
    return std::get<Sketch>(feature->data);
}

// Changes to one sketch, with IDs from the document and values in its units.
class Editor {
  public:
    Editor(Document& document, Sketch& sketch)
        : document_(document), sketch_(sketch), scale_(millimetersPer(document.units)) {}

    // The point where `cut` lies: a sketch point already there, or a new one on the curves
    // through it.
    std::string point(const Cut& cut) {
        for (const PlacedPoint& placed : pointsOf(document_, sketch_)) {
            if (distance(placed.u, placed.v, cut.u, cut.v) <= kSamePlace) {
                return placed.id;
            }
        }
        std::string id = point(cut.u, cut.v);
        for (const std::string& curve : cut.curves) {
            constrain("coincident", {id, curve});
        }
        return id;
    }

    std::string point(double u, double v) {
        std::string id = document_.newId("p");
        sketch_.points.push_back({.id = id, .x = u / scale_, .y = v / scale_});
        return id;
    }

    void constrain(std::string type, std::vector<std::string> entities) {
        sketch_.constraints.push_back(
            {.id = document_.newId("k"), .type = std::move(type), .entities = std::move(entities)});
    }

    // Removes a point that no curve uses any more, with the constraints on it.
    void dropIfUnused(const std::string& point) {
        const bool used =
            std::any_of(sketch_.lines.begin(), sketch_.lines.end(),
                        [&](const SketchLine& l) { return l.start == point || l.end == point; }) ||
            std::any_of(sketch_.arcs.begin(), sketch_.arcs.end(),
                        [&](const SketchArc& a) {
                            return a.center == point || a.start == point || a.end == point;
                        }) ||
            std::any_of(sketch_.circles.begin(), sketch_.circles.end(),
                        [&](const SketchCircle& c) { return c.center == point; });
        if (!used) {
            std::erase_if(sketch_.points, [&](const SketchPoint& p) { return p.id == point; });
            dropConstraints({point});
        }
    }

    void dropConstraints(const std::set<std::string>& entities) {
        std::erase_if(sketch_.constraints, [&](const SketchConstraint& constraint) {
            return std::any_of(constraint.entities.begin(), constraint.entities.end(),
                               [&](const std::string& e) { return entities.contains(e); });
        });
    }

    Document& document() {
        return document_;
    }
    Sketch& sketch() {
        return sketch_;
    }
    [[nodiscard]] double scale() const {
        return scale_;
    }

  private:
    Document& document_;
    Sketch& sketch_;
    double scale_;
};

template <typename Curves>
auto* find(Curves& curves, const std::string& id) {
    const auto it = std::find_if(curves.begin(), curves.end(),
                                 [&](const auto& curve) { return curve.id == id; });
    return it == curves.end() ? nullptr : &*it;
}

}  // namespace

void deleteSketchEntities(Document& document, const std::string& sketchId,
                          const std::vector<std::string>& entities) {
    Sketch& sketch = sketchIn(document, sketchId);
    Editor editor(document, sketch);
    std::set<std::string> points;
    std::set<std::string> curves;
    for (const std::string& id : entities) {
        if (find(sketch.points, id) != nullptr) {
            points.insert(id);
        } else if (find(sketch.lines, id) != nullptr || find(sketch.arcs, id) != nullptr ||
                   find(sketch.circles, id) != nullptr) {
            curves.insert(id);
        } else {
            throw Error(ErrorCode::InvalidArgument,
                        id + " is not a point or curve of sketch " + sketchId);
        }
    }
    // Curves on deleted points go too; the points of deleted curves may be left unused.
    std::set<std::string> loose;
    const auto touches = [&](std::initializer_list<const std::string*> ends) {
        return std::any_of(ends.begin(), ends.end(),
                           [&](const std::string* p) { return points.contains(*p); });
    };
    for (const SketchLine& line : sketch.lines) {
        if (curves.contains(line.id) || touches({&line.start, &line.end})) {
            curves.insert(line.id);
            loose.insert({line.start, line.end});
        }
    }
    for (const SketchArc& arc : sketch.arcs) {
        if (curves.contains(arc.id) || touches({&arc.center, &arc.start, &arc.end})) {
            curves.insert(arc.id);
            loose.insert({arc.center, arc.start, arc.end});
        }
    }
    for (const SketchCircle& circle : sketch.circles) {
        if (curves.contains(circle.id) || touches({&circle.center})) {
            curves.insert(circle.id);
            loose.insert(circle.center);
        }
    }
    const auto gone = [&](const auto& curve) { return curves.contains(curve.id); };
    std::erase_if(sketch.lines, gone);
    std::erase_if(sketch.arcs, gone);
    std::erase_if(sketch.circles, gone);
    std::erase_if(sketch.points, [&](const SketchPoint& p) { return points.contains(p.id); });
    std::set<std::string> removed = curves;
    removed.insert(points.begin(), points.end());
    editor.dropConstraints(removed);
    for (const std::string& point : loose) {
        if (!points.contains(point)) {
            editor.dropIfUnused(point);
        }
    }
}

bool trimSketch(Document& document, const std::string& sketchId, double u, double v,
                double radius) {
    Sketch& sketch = sketchIn(document, sketchId);
    const auto plan = planTrim(document, sketch, u, v, radius);
    if (!plan) {
        return false;
    }
    if (!plan->from && !plan->to) {
        deleteSketchEntities(document, sketchId, {plan->curve});
        return true;
    }
    Editor editor(document, sketch);
    if (SketchLine* line = find(sketch.lines, plan->curve)) {
        if (!plan->from) {
            const std::string old = line->start;
            line->start = editor.point(*plan->to);
            editor.dropIfUnused(old);
        } else if (!plan->to) {
            const std::string old = line->end;
            line->end = editor.point(*plan->from);
            editor.dropIfUnused(old);
        } else {
            // The middle goes: the line keeps its start, a new line takes the rest.
            const std::string end = line->end;
            const std::string id = line->id;
            const bool construction = line->construction;
            line->end = editor.point(*plan->from);
            const std::string start = editor.point(*plan->to);
            const std::string piece = document.newId("l");
            sketch.lines.push_back(
                {.id = piece, .start = start, .end = end, .construction = construction});
            // The rest keeps the line's direction: its direction constraints, or one parallel
            // to the line.
            std::vector<SketchConstraint> copies;
            for (const SketchConstraint& constraint : sketch.constraints) {
                const bool direction =
                    constraint.type == "horizontal" || constraint.type == "vertical" ||
                    constraint.type == "parallel" || constraint.type == "perpendicular";
                if (direction && std::find(constraint.entities.begin(), constraint.entities.end(),
                                           id) != constraint.entities.end()) {
                    copies.push_back(constraint);
                }
            }
            if (copies.empty()) {
                editor.constrain("parallel", {id, piece});
            }
            for (SketchConstraint& copy : copies) {
                std::replace(copy.entities.begin(), copy.entities.end(), id, piece);
                editor.constrain(copy.type, copy.entities);
            }
        }
        return true;
    }
    if (SketchArc* arc = find(sketch.arcs, plan->curve)) {
        if (!plan->from) {
            const std::string old = arc->start;
            arc->start = editor.point(*plan->to);
            editor.dropIfUnused(old);
        } else if (!plan->to) {
            const std::string old = arc->end;
            arc->end = editor.point(*plan->from);
            editor.dropIfUnused(old);
        } else {
            const std::string end = arc->end;
            const std::string center = arc->center;
            const bool construction = arc->construction;
            arc->end = editor.point(*plan->from);
            sketch.arcs.push_back({.id = document.newId("a"),
                                   .center = center,
                                   .start = editor.point(*plan->to),
                                   .end = end,
                                   .construction = construction});
        }
        return true;
    }
    // A circle: what is left runs from the cut after the removed piece round to the one before.
    SketchCircle* circle = find(sketch.circles, plan->curve);
    if (circle == nullptr || !plan->from || !plan->to) {
        return false;
    }
    const SketchArc arc{.id = circle->id,
                        .center = circle->center,
                        .start = editor.point(*plan->to),
                        .end = editor.point(*plan->from),
                        .construction = circle->construction};
    std::erase_if(sketch.circles, [&](const SketchCircle& c) { return c.id == arc.id; });
    sketch.arcs.push_back(arc);
    return true;
}

bool extendSketch(Document& document, const std::string& sketchId, double u, double v,
                  double radius) {
    Sketch& sketch = sketchIn(document, sketchId);
    const auto plan = planExtend(document, sketch, u, v, radius);
    if (!plan) {
        return false;
    }
    Editor editor(document, sketch);
    SketchLine* line = find(sketch.lines, plan->line);
    std::string& end = plan->atStart ? line->start : line->end;
    const std::string old = end;
    end = editor.point(plan->hit);
    editor.dropIfUnused(old);
    return true;
}

bool filletSketchCorner(Document& document, const std::string& sketchId, const std::string& point,
                        double setback) {
    Sketch& sketch = sketchIn(document, sketchId);
    const auto plan = planFillet(document, sketch, point, setback);
    if (!plan) {
        return false;
    }
    Editor editor(document, sketch);
    const std::string first = editor.point(plan->u1, plan->v1);
    const std::string second = editor.point(plan->u2, plan->v2);
    const std::string center = editor.point(plan->cu, plan->cv);
    const std::pair<std::string, std::string> ends[] = {{plan->first, first},
                                                        {plan->second, second}};
    for (const auto& [line, end] : ends) {
        SketchLine* edited = find(sketch.lines, line);
        (edited->start == point ? edited->start : edited->end) = end;
    }
    const std::string arc = document.newId("a");
    sketch.arcs.push_back({.id = arc,
                           .center = center,
                           .start = plan->ccw ? first : second,
                           .end = plan->ccw ? second : first,
                           .construction = false});
    editor.constrain("tangent", {plan->first, arc});
    editor.constrain("tangent", {plan->second, arc});
    editor.dropIfUnused(point);
    return true;
}

void moveSketchEntities(Document& document, const std::string& sketchId,
                        const std::vector<std::string>& entities, double du, double dv, bool copy) {
    Sketch& sketch = sketchIn(document, sketchId);
    Editor editor(document, sketch);
    // The points that move: the chosen ones and those of the chosen curves.
    std::vector<std::string> moving;
    const auto add = [&](const std::string& point) {
        if (std::find(moving.begin(), moving.end(), point) == moving.end()) {
            moving.push_back(point);
        }
    };
    for (const std::string& id : entities) {
        if (find(sketch.points, id) != nullptr) {
            add(id);
        } else if (const SketchLine* line = find(sketch.lines, id)) {
            add(line->start);
            add(line->end);
        } else if (const SketchArc* arc = find(sketch.arcs, id)) {
            add(arc->center);
            add(arc->start);
            add(arc->end);
        } else if (const SketchCircle* circle = find(sketch.circles, id)) {
            add(circle->center);
        } else {
            throw Error(ErrorCode::InvalidArgument,
                        id + " is not a point or curve of sketch " + sketchId);
        }
    }
    const auto moved = [&](const SketchPoint& point) {
        return std::pair{lengthInMillimeters(point.x, document) + du,
                         lengthInMillimeters(point.y, document) + dv};
    };
    if (!copy) {
        for (SketchPoint& point : sketch.points) {
            if (std::find(moving.begin(), moving.end(), point.id) != moving.end()) {
                const auto [u, v] = moved(point);
                point.x = u / editor.scale();
                point.y = v / editor.scale();
            }
        }
        return;
    }
    // Copies: new points, new curves on them, and copies of the constraints among them.
    std::map<std::string, std::string> copies;
    for (const std::string& id : moving) {
        const auto [u, v] = moved(*find(sketch.points, id));
        copies[id] = editor.point(u, v);
    }
    for (const std::string& id : entities) {
        if (const SketchLine* line = find(sketch.lines, id)) {
            const SketchLine source = *line;
            copies[id] = document.newId("l");
            sketch.lines.push_back({.id = copies[id],
                                    .start = copies[source.start],
                                    .end = copies[source.end],
                                    .construction = source.construction});
        } else if (const SketchArc* arc = find(sketch.arcs, id)) {
            const SketchArc source = *arc;
            copies[id] = document.newId("a");
            sketch.arcs.push_back({.id = copies[id],
                                   .center = copies[source.center],
                                   .start = copies[source.start],
                                   .end = copies[source.end],
                                   .construction = source.construction});
        } else if (const SketchCircle* circle = find(sketch.circles, id)) {
            const SketchCircle source = *circle;
            copies[id] = document.newId("c");
            sketch.circles.push_back({.id = copies[id],
                                      .center = copies[source.center],
                                      .radius = source.radius,
                                      .construction = source.construction});
        }
    }
    const std::vector<SketchConstraint> constraints = sketch.constraints;
    for (const SketchConstraint& constraint : constraints) {
        if (std::all_of(constraint.entities.begin(), constraint.entities.end(),
                        [&](const std::string& e) { return copies.contains(e); })) {
            std::vector<std::string> bound;
            for (const std::string& e : constraint.entities) {
                bound.push_back(copies[e]);
            }
            editor.constrain(constraint.type, std::move(bound));
        }
    }
}

}  // namespace facetry::core
