// The editing tools of SketchTool (#51): trim, extend, fillet, move and copy. They act through the
// functions of sketch_edit.hpp and preview what those would do (sketch_edit_plan.hpp).
#include "facetry/core/sketch_edit.hpp"
#include "facetry/core/sketch_tool.hpp"

#include "sketch_edit_plan.hpp"
#include "sketch_geometry.hpp"

#include <cmath>

namespace facetry::core {

using namespace sketching;

namespace {

// A corner a fillet can round: a point where exactly two lines end.
bool isCorner(const Document& document, const Sketch& sketch, const std::string& point) {
    constexpr double kTinySetback = 1e-6;
    return !point.empty() && planFillet(document, sketch, point, kTinySetback).has_value();
}

}  // namespace

bool SketchTool::trimOrExtend(Session& session, PointerEvent event, double u, double v,
                              double radius) {
    pointer_ = Spot{.u = u, .v = v};
    const auto act = [&] {
        const Sketch& sketch = sketchOf(session.document(), sketch_);
        if (kind_ == SketchToolKind::Trim ? !planTrim(session.document(), sketch, u, v, radius)
                                          : !planExtend(session.document(), sketch, u, v, radius)) {
            return false;
        }
        session.edit(
            [&](Document& document) {
                if (kind_ == SketchToolKind::Trim) {
                    trimSketch(document, sketch_, u, v, radius);
                } else {
                    extendSketch(document, sketch_, u, v, radius);
                }
            },
            kind_ == SketchToolKind::Trim ? "Trim" : "Extend");
        return true;
    };
    switch (event) {
        case PointerEvent::Down:
            pressed_ = pointer_;
            // Trimming starts at once, so a drag trims every curve it crosses.
            return kind_ == SketchToolKind::Trim && act();
        case PointerEvent::Move:
            return pressed_ && kind_ == SketchToolKind::Trim && act();
        case PointerEvent::Up: {
            const bool click = pressed_ && distance(pressed_->u, pressed_->v, u, v) <= radius;
            pressed_.reset();
            return kind_ == SketchToolKind::Extend && click && act();
        }
    }
    return false;
}

std::vector<std::string> SketchTool::moving(const Session& session, const Spot& base) const {
    const Document& document = session.document();
    const Sketch& sketch = sketchOf(document, sketch_);
    std::vector<std::string> entities = selection_;
    if (entities.empty() && !base.point.empty()) {
        entities = {base.point};
    } else if (entities.empty()) {
        const std::string curve =
            nearestCurve(snapGeometry(document, sketch), base.u, base.v, lastRadius_).id();
        if (!curve.empty()) {
            entities = {curve};
        }
    }
    // Projections place what they drive: it can be copied, but not moved.
    if (kind_ == SketchToolKind::Move) {
        std::erase_if(entities,
                      [&](const std::string& id) { return sketch.projectionOf(id) != nullptr; });
    }
    return entities;
}

bool SketchTool::editClick(Session& session, const Spot& spot, double snapRadius) {
    const Document& document = session.document();
    const Sketch& sketch = sketchOf(document, sketch_);
    if (kind_ == SketchToolKind::Fillet) {
        if (fixed_.empty()) {
            if (isCorner(document, sketch, spot.point)) {
                fixed_ = {spot};
            }
            return false;
        }
        const Spot corner = fixed_.front();
        double setback = distance(corner.u, corner.v, spot.u, spot.v);
        if (setback <= snapRadius) {
            // A second click on the corner repeats the last fillet's size.
            if (setback_ <= 0.0) {
                return false;
            }
            setback = setback_;
        }
        if (!planFillet(document, sketch, corner.point, setback)) {
            return false;  // too big: the corner stays chosen
        }
        session.edit(
            [&](Document& edited) { filletSketchCorner(edited, sketch_, corner.point, setback); },
            "Fillet");
        setback_ = setback;
        fixed_.clear();
        return true;
    }

    // Move and copy: a base point, then the target.
    if (fixed_.empty()) {
        std::vector<std::string> entities = moving(session, spot);
        if (entities.empty()) {
            return false;
        }
        if (selection_.empty()) {
            selection_ = std::move(entities);
            pickedSelection_ = true;
        }
        fixed_ = {spot};
        return false;
    }
    const double du = spot.u - fixed_.front().u;
    const double dv = spot.v - fixed_.front().v;
    if (std::hypot(du, dv) <= kEpsilon) {
        return false;
    }
    const bool copy = kind_ == SketchToolKind::Copy;
    session.edit(
        [&](Document& edited) { moveSketchEntities(edited, sketch_, selection_, du, dv, copy); },
        copy ? "Copy" : "Move");
    fixed_.clear();
    if (pickedSelection_) {
        selection_.clear();
        pickedSelection_ = false;
    }
    return true;
}

double SketchTool::filletRadius(const Session& session) const {
    const std::vector<Spot> spots = placed(lastRadius_);
    if (kind_ != SketchToolKind::Fillet || spots.empty() || !pointer_) {
        return 0.0;
    }
    const Spot& corner = spots.front();
    const auto plan =
        planFillet(session.document(), sketchOf(session.document(), sketch_), corner.point,
                   distance(corner.u, corner.v, pointer_->u, pointer_->v));
    return plan ? distance(plan->cu, plan->cv, plan->u1, plan->v1) : 0.0;
}

SketchTool::EditPreview SketchTool::editPreview(const Session& session) const {
    EditPreview preview;
    if (!pointer_) {
        return preview;
    }
    const Document& document = session.document();
    const Sketch& sketch = sketchOf(document, sketch_);
    const Spot& p = *pointer_;
    switch (kind_) {
        case SketchToolKind::Trim:
            // The piece that goes, dashed.
            if (const auto plan = planTrim(document, sketch, p.u, p.v, lastRadius_)) {
                preview.guides.push_back(plan->removed);
            }
            return preview;
        case SketchToolKind::Extend:
            if (const auto plan = planExtend(document, sketch, p.u, p.v, lastRadius_)) {
                preview.curves.push_back(plan->added);
                preview.points.emplace_back(plan->hit.u, plan->hit.v);
            }
            return preview;
        case SketchToolKind::Fillet: {
            const std::vector<Spot> spots = placed(lastRadius_);
            if (spots.empty()) {
                if (isCorner(document, sketch, p.point)) {
                    preview.points.emplace_back(p.u, p.v);
                }
                return preview;
            }
            const Spot& corner = spots.front();
            preview.points.emplace_back(corner.u, corner.v);
            const double setback = distance(corner.u, corner.v, p.u, p.v);
            if (const auto plan = planFillet(document, sketch, corner.point, setback)) {
                preview.curves.push_back(plan->arc);
            }
            preview.points.emplace_back(p.u, p.v);
            return preview;
        }
        case SketchToolKind::Move:
        case SketchToolKind::Copy: {
            const std::vector<Spot> spots = placed(lastRadius_);
            preview.points.emplace_back(p.u, p.v);
            if (spots.empty()) {
                return preview;
            }
            const Spot& base = spots.front();
            preview.points.emplace_back(base.u, base.v);
            preview.curves =
                movedCurves(document, sketch, moving(session, base), p.u - base.u, p.v - base.v);
            preview.guides.push_back(lineSegment(base.u, base.v, p.u, p.v));
            return preview;
        }
        default:
            return preview;
    }
}

}  // namespace facetry::core
