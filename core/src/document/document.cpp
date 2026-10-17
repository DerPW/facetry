#include "facetry/core/document.hpp"

#include "facetry/core/error.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <set>
#include <string>

namespace facetry::core {
namespace {

[[noreturn]] void invalid(const std::string& message) {
    throw Error(ErrorCode::InvalidArgument, message);
}

// The number in an ID like "f12", or 0 if it does not end in one.
std::uint64_t idNumber(std::string_view id) {
    std::size_t start = id.size();
    while (start > 0 && std::isdigit(static_cast<unsigned char>(id[start - 1])) != 0) {
        --start;
    }
    if (start == id.size() || id.size() - start > 18) {
        return 0;
    }
    return std::stoull(std::string(id.substr(start)));
}

void checkValue(const Value& value, const std::string& what) {
    if (value.parameter.empty() && !std::isfinite(value.number)) {
        invalid(what + " must be a finite number");
    }
}

void validateSketch(const Feature& feature, const Sketch& sketch) {
    const std::string context = "Sketch " + feature.id;
    const bool onFace = sketch.plane == "face";
    if (!onFace && sketch.plane != "xy" && sketch.plane != "xz" && sketch.plane != "yz") {
        invalid(context + ": \"" + sketch.plane + "\" is not a sketch plane");
    }
    if (sketch.face.size() != (onFace ? 1U : 0U)) {
        invalid(context + (onFace ? ": a sketch on a face needs one face reference"
                                  : ": only a sketch on a face has a face reference"));
    }
    if (onFace && sketch.face[0].kind != ElementKind::Face) {
        invalid(context + ": the sketch plane must be a face");
    }
    // Every entity ID is unique within the sketch; points and curves share one namespace.
    std::set<std::string> ids;
    std::set<std::string> geometry;  // points and curves, what constraints can bind
    std::set<std::string> points;
    const auto add = [&](const std::string& id, const char* what) {
        if (id.empty() || !ids.insert(id).second) {
            invalid(context + ": " + what + " IDs must be unique and not empty (\"" + id + "\")");
        }
        if (std::string_view(what) != "constraint") {
            geometry.insert(id);
        }
    };
    const auto point = [&](const std::string& id, const std::string& curve) {
        if (!points.contains(id)) {
            invalid(context + ": " + curve + " uses the point " + id + ", which does not exist");
        }
    };
    for (const SketchPoint& p : sketch.points) {
        add(p.id, "point");
        points.insert(p.id);
        checkValue(p.x, context + ": point " + p.id + " x");
        checkValue(p.y, context + ": point " + p.id + " y");
    }
    for (const SketchLine& line : sketch.lines) {
        add(line.id, "line");
        point(line.start, "line " + line.id);
        point(line.end, "line " + line.id);
        if (line.start == line.end) {
            invalid(context + ": line " + line.id + " starts and ends at the same point");
        }
    }
    for (const SketchArc& arc : sketch.arcs) {
        add(arc.id, "arc");
        for (const std::string* p : {&arc.center, &arc.start, &arc.end}) {
            point(*p, "arc " + arc.id);
        }
        if (arc.start == arc.end || arc.center == arc.start || arc.center == arc.end) {
            invalid(context + ": arc " + arc.id + " needs three different points");
        }
    }
    for (const SketchCircle& circle : sketch.circles) {
        add(circle.id, "circle");
        point(circle.center, "circle " + circle.id);
        checkValue(circle.radius, context + ": circle " + circle.id + " radius");
    }
    for (const SketchConstraint& constraint : sketch.constraints) {
        add(constraint.id, "constraint");
        const auto* type =
            std::find_if(std::begin(kSketchConstraintTypes), std::end(kSketchConstraintTypes),
                         [&](const SketchConstraintType& t) { return t.name == constraint.type; });
        if (type == std::end(kSketchConstraintTypes)) {
            invalid(context + ": constraint " + constraint.id + " has the unknown type \"" +
                    constraint.type + "\"");
        }
        if (constraint.entities.size() < type->minEntities ||
            constraint.entities.size() > type->maxEntities) {
            invalid(context + ": constraint " + constraint.id + " (" + constraint.type +
                    ") binds the wrong number of entities");
        }
        for (const std::string& entity : constraint.entities) {
            if (!geometry.contains(entity)) {
                invalid(context + ": constraint " + constraint.id + " binds " + entity +
                        ", which is not a point or curve of the sketch");
            }
        }
    }
}

}  // namespace

std::span<const OriginItem> originItems() noexcept {
    static constexpr OriginItem kItems[] = {
        {"xy", "XY plane", "plane"}, {"xz", "XZ plane", "plane"}, {"yz", "YZ plane", "plane"},
        {"x", "X axis", "axis"},     {"y", "Y axis", "axis"},     {"z", "Z axis", "axis"},
        {"o", "Origin", "point"},
    };
    return kItems;
}

std::vector<Reference>* Feature::references() noexcept {
    if (auto* fillet = std::get_if<Fillet>(&data)) {
        return &fillet->edges;
    }
    if (auto* sketch = std::get_if<Sketch>(&data)) {
        return &sketch->face;
    }
    return nullptr;
}

const std::vector<Reference>* Feature::references() const noexcept {
    if (const auto* fillet = std::get_if<Fillet>(&data)) {
        return &fillet->edges;
    }
    if (const auto* sketch = std::get_if<Sketch>(&data)) {
        return &sketch->face;
    }
    return nullptr;
}

void Feature::forEachValue(const std::function<void(Value&)>& visit) {
    if (auto* sketch = std::get_if<Sketch>(&data)) {
        for (SketchPoint& point : sketch->points) {
            visit(point.x);
            visit(point.y);
        }
        for (SketchCircle& circle : sketch->circles) {
            visit(circle.radius);
        }
    } else if (auto* extrude = std::get_if<Extrude>(&data)) {
        visit(extrude->distance);
    } else if (auto* fillet = std::get_if<Fillet>(&data)) {
        visit(fillet->radius);
    }
}

std::string Document::newId(std::string_view prefix) {
    // Skip past IDs that were set without newId(), e.g. by an importer or a test.
    std::uint64_t used = 0;
    for (const Feature& feature : root.timeline) {
        used = std::max(used, idNumber(feature.id));
        if (const auto* sketch = std::get_if<Sketch>(&feature.data)) {
            for (const SketchPoint& point : sketch->points) {
                used = std::max(used, idNumber(point.id));
            }
            for (const SketchLine& line : sketch->lines) {
                used = std::max(used, idNumber(line.id));
            }
            for (const SketchArc& arc : sketch->arcs) {
                used = std::max(used, idNumber(arc.id));
            }
            for (const SketchCircle& circle : sketch->circles) {
                used = std::max(used, idNumber(circle.id));
            }
            for (const SketchConstraint& constraint : sketch->constraints) {
                used = std::max(used, idNumber(constraint.id));
            }
        }
    }
    nextId = std::max(nextId, used + 1);
    return std::string(prefix) + std::to_string(nextId++);
}

Feature* Document::findFeature(std::string_view id) noexcept {
    const auto it = std::find_if(root.timeline.begin(), root.timeline.end(),
                                 [&](const Feature& feature) { return feature.id == id; });
    return it == root.timeline.end() ? nullptr : &*it;
}

const Feature* Document::findFeature(std::string_view id) const noexcept {
    const auto it = std::find_if(root.timeline.begin(), root.timeline.end(),
                                 [&](const Feature& feature) { return feature.id == id; });
    return it == root.timeline.end() ? nullptr : &*it;
}

const Parameter* Document::findParameter(std::string_view name) const noexcept {
    const auto it =
        std::find_if(parameters.begin(), parameters.end(),
                     [&](const Parameter& parameter) { return parameter.name == name; });
    return it == parameters.end() ? nullptr : &*it;
}

std::vector<const Feature*> Document::bodyFeatures() const {
    std::vector<const Feature*> result;
    for (const Feature& feature : root.timeline) {
        if (std::holds_alternative<Extrude>(feature.data)) {
            result.push_back(&feature);
        }
    }
    return result;
}

double millimetersPer(LengthUnit unit) noexcept {
    switch (unit) {
        case LengthUnit::Millimeter:
            return 1.0;
        case LengthUnit::Centimeter:
            return 10.0;
        case LengthUnit::Meter:
            return 1000.0;
        case LengthUnit::Inch:
            return 25.4;
    }
    return 1.0;
}

std::string_view unitSymbol(LengthUnit unit) noexcept {
    switch (unit) {
        case LengthUnit::Millimeter:
            return "mm";
        case LengthUnit::Centimeter:
            return "cm";
        case LengthUnit::Meter:
            return "m";
        case LengthUnit::Inch:
            return "in";
    }
    return "mm";
}

std::optional<LengthUnit> unitFromSymbol(std::string_view symbol) noexcept {
    for (const LengthUnit unit :
         {LengthUnit::Millimeter, LengthUnit::Centimeter, LengthUnit::Meter, LengthUnit::Inch}) {
        if (unitSymbol(unit) == symbol) {
            return unit;
        }
    }
    return std::nullopt;
}

void convertUnits(Document& document, LengthUnit units) {
    const double factor = millimetersPer(document.units) / millimetersPer(units);
    document.units = units;
    if (factor == 1.0) {
        return;
    }
    // Values that name a parameter follow it.
    const auto convert = [factor](Value& value) {
        if (value.parameter.empty()) {
            value.number *= factor;
        }
    };
    for (Parameter& parameter : document.parameters) {
        parameter.value *= factor;
    }
    for (Feature& feature : document.root.timeline) {
        feature.forEachValue(convert);
    }
}

std::string Document::bodyName(std::string_view body) const {
    const auto setting = std::find_if(root.bodies.begin(), root.bodies.end(),
                                      [&](const BodySettings& s) { return s.id == body; });
    if (setting != root.bodies.end() && !setting->name.empty()) {
        return setting->name;
    }
    const auto features = bodyFeatures();
    const auto it = std::find_if(features.begin(), features.end(),
                                 [&](const Feature* feature) { return feature->id == body; });
    return "Body " + std::to_string(it - features.begin() + 1);
}

bool Document::bodyVisible(std::string_view body) const {
    const auto setting = std::find_if(root.bodies.begin(), root.bodies.end(),
                                      [&](const BodySettings& s) { return s.id == body; });
    return setting == root.bodies.end() || setting->visible;
}

double Document::bodyOpacity(std::string_view body) const {
    const auto setting = std::find_if(root.bodies.begin(), root.bodies.end(),
                                      [&](const BodySettings& s) { return s.id == body; });
    return setting == root.bodies.end() ? 1.0 : setting->opacity;
}

void validate(const Document& document) {
    std::set<std::string> names;
    for (const Parameter& parameter : document.parameters) {
        if (parameter.name.empty() || !names.insert(parameter.name).second) {
            invalid("Parameter names must be unique and not empty (\"" + parameter.name + "\")");
        }
        if (!std::isfinite(parameter.value)) {
            invalid("Parameter " + parameter.name + " must be a finite number");
        }
    }

    std::set<std::string> ids;
    for (const Feature& feature : document.root.timeline) {
        if (feature.id.empty() || !ids.insert(feature.id).second) {
            invalid("Feature IDs must be unique and not empty (\"" + feature.id + "\")");
        }
        if (feature.namingVersion < 1) {
            invalid("Feature " + feature.id + " has an invalid naming version");
        }
        if (feature.namingVersion > kNamingVersion) {
            throw Error(ErrorCode::Unsupported, "Feature " + feature.id + " uses naming version " +
                                                    std::to_string(feature.namingVersion) +
                                                    " from a newer version of Facetry");
        }
        if (const auto* sketch = std::get_if<Sketch>(&feature.data)) {
            validateSketch(feature, *sketch);
        } else if (const auto* extrude = std::get_if<Extrude>(&feature.data)) {
            checkValue(extrude->distance, "Extrude " + feature.id + " distance");
            const std::set<std::string> regions(extrude->regions.begin(), extrude->regions.end());
            if (regions.size() != extrude->regions.size() || regions.contains("")) {
                invalid("Extrude " + feature.id + ": profiles must be named once each");
            }
        } else if (const auto* fillet = std::get_if<Fillet>(&feature.data)) {
            checkValue(fillet->radius, "Fillet " + feature.id + " radius");
            for (const Reference& edge : fillet->edges) {
                if (edge.kind != ElementKind::Edge || edge.body.empty() || edge.name.empty()) {
                    invalid("Fillet " + feature.id + " must reference edges by body and name");
                }
            }
        }
    }

    std::set<std::string> bodies;
    for (const BodySettings& body : document.root.bodies) {
        if (body.id.empty() || !bodies.insert(body.id).second) {
            invalid("Body settings must have unique, non-empty IDs (\"" + body.id + "\")");
        }
        if (!(body.opacity >= 0.0 && body.opacity <= 1.0)) {
            invalid("The opacity of body " + body.id + " must be between 0 and 1");
        }
    }
    for (const std::string& item : document.root.visibleOrigin) {
        const auto items = originItems();
        if (std::none_of(items.begin(), items.end(),
                         [&](const OriginItem& origin) { return origin.id == item; })) {
            invalid("\"" + item + "\" is not an origin item");
        }
    }

    if (document.rollback && *document.rollback > document.root.timeline.size()) {
        invalid("The rollback marker is past the end of the timeline");
    }
}

}  // namespace facetry::core
