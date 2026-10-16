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
    std::set<std::string> points;
    for (const SketchPoint& point : sketch.points) {
        if (point.id.empty() || !points.insert(point.id).second) {
            invalid(context + ": point IDs must be unique and not empty (\"" + point.id + "\")");
        }
        checkValue(point.x, context + ": point " + point.id + " x");
        checkValue(point.y, context + ": point " + point.id + " y");
    }
    std::set<std::string> lines;
    for (const SketchLine& line : sketch.lines) {
        if (line.id.empty() || !lines.insert(line.id).second) {
            invalid(context + ": line IDs must be unique and not empty (\"" + line.id + "\")");
        }
        if (!points.contains(line.start) || !points.contains(line.end)) {
            invalid(context + ": line " + line.id + " uses a point that does not exist");
        }
        if (line.start == line.end) {
            invalid(context + ": line " + line.id + " starts and ends at the same point");
        }
    }
}

}  // namespace

std::vector<Reference>* Feature::references() noexcept {
    if (auto* fillet = std::get_if<Fillet>(&data)) {
        return &fillet->edges;
    }
    return nullptr;
}

const std::vector<Reference>* Feature::references() const noexcept {
    if (const auto* fillet = std::get_if<Fillet>(&data)) {
        return &fillet->edges;
    }
    return nullptr;
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
        } else if (const auto* fillet = std::get_if<Fillet>(&feature.data)) {
            checkValue(fillet->radius, "Fillet " + feature.id + " radius");
            for (const Reference& edge : fillet->edges) {
                if (edge.kind != ElementKind::Edge || edge.body.empty() || edge.name.empty()) {
                    invalid("Fillet " + feature.id + " must reference edges by body and name");
                }
            }
        }
    }

    if (document.rollback && *document.rollback > document.root.timeline.size()) {
        invalid("The rollback marker is past the end of the timeline");
    }
}

}  // namespace facetry::core
