#include "facetry/core/regeneration.hpp"

#include "facetry/core/error.hpp"

#include "document_json.hpp"

#include <algorithm>
#include <exception>
#include <iomanip>
#include <map>
#include <sstream>
#include <string>
#include <utility>

namespace facetry::core {
namespace {

// A length in millimeters, the kernel's unit.
double length(const Value& value, const Document& document) {
    const double scale = millimetersPer(document.units);
    if (value.parameter.empty()) {
        return value.number * scale;
    }
    const Parameter* parameter = document.findParameter(value.parameter);
    if (parameter == nullptr) {
        throw Error(ErrorCode::InvalidArgument,
                    "The parameter \"" + value.parameter + "\" does not exist");
    }
    return parameter->value * scale;
}

// What a feature's result depends on, besides the features before it: its definition with
// parameter values filled in, and the units. Names and shadows do not change the result.
std::string cacheKey(Feature feature, const Document& document) {
    const auto fill = [&](Value& value) {
        if (const Parameter* parameter = document.findParameter(value.parameter)) {
            value = Value(parameter->value);
        }
    };
    feature.forEachValue(fill);
    if (auto* references = feature.references()) {
        for (Reference& reference : *references) {
            reference.shadow.reset();
        }
    }
    feature.name.clear();
    return std::string(unitSymbol(document.units)) + json::featureToJson(feature).dump();
}

// For messages, in the document's units.
std::string describe(const ElementGeometry& geometry, LengthUnit units) {
    const double scale = millimetersPer(units);
    std::ostringstream text;
    text << std::setprecision(6) << geometry.type << " at (" << geometry.centroid.x / scale << ", "
         << geometry.centroid.y / scale << ", " << geometry.centroid.z / scale << ") "
         << unitSymbol(units);
    return text.str();
}

std::optional<std::uint32_t> resolveReference(const NamedShape& body, const Reference& reference,
                                              LengthUnit units, FeatureResult& result);

// The sketch's plane: an origin plane, or the face its reference names (resolved like a
// fillet's edges, with the shadow as a fallback).
SketchFrame sketchFrame(const Sketch& sketch, const Document& document, const ModelState& model,
                        FeatureResult& result) {
    if (sketch.plane != "face") {
        return originPlaneFrame(sketch.plane);
    }
    const Reference& face = sketch.face.at(0);
    result.resolved.assign(1, std::nullopt);
    const auto body = model.bodies.find(face.body);
    std::optional<std::uint32_t> id;
    if (body == model.bodies.end()) {
        result.messages.push_back("The body " + face.body + " does not exist.");
    } else {
        id = resolveReference(*body->second, face, document.units, result);
    }
    if (!id) {
        result.brokenReferences.push_back(0);
        throw Error(ErrorCode::InvalidArgument, "Select the face the sketch lies on again.");
    }
    result.resolved[0] =
        Shadow{.id = *id, .geometry = elementGeometry(body->second->shape, face.kind, *id)};
    return planarFaceFrame(*body->second, *id);
}

SketchGeometry sketchGeometry(const Feature& feature, const Sketch& sketch,
                              const Document& document, const ModelState& model,
                              FeatureResult& result) {
    SketchGeometry geometry{.frame = sketchFrame(sketch, document, model, result), .curves = {}};
    std::map<std::string, std::pair<double, double>> points;
    for (const SketchPoint& point : sketch.points) {
        points[point.id] = {length(point.x, document), length(point.y, document)};
    }
    const auto name = [&](const std::string& curve) { return feature.id + "/" + curve; };
    for (const SketchLine& line : sketch.lines) {
        const auto [x0, y0] = points.at(line.start);
        const auto [x1, y1] = points.at(line.end);
        geometry.curves.push_back(
            {.segment = {.name = name(line.id), .x0 = x0, .y0 = y0, .x1 = x1, .y1 = y1},
             .start = line.start,
             .end = line.end,
             .construction = line.construction});
    }
    for (const SketchArc& arc : sketch.arcs) {
        const auto [cx, cy] = points.at(arc.center);
        const auto [x0, y0] = points.at(arc.start);
        const auto [x1, y1] = points.at(arc.end);
        geometry.curves.push_back({.segment = {.name = name(arc.id),
                                               .x0 = x0,
                                               .y0 = y0,
                                               .x1 = x1,
                                               .y1 = y1,
                                               .kind = ProfileSegment::Kind::Arc,
                                               .cx = cx,
                                               .cy = cy},
                                   .start = arc.start,
                                   .end = arc.end,
                                   .construction = arc.construction});
    }
    for (const SketchCircle& circle : sketch.circles) {
        const auto [cx, cy] = points.at(circle.center);
        geometry.curves.push_back({.segment = {.name = name(circle.id),
                                               .kind = ProfileSegment::Kind::Circle,
                                               .cx = cx,
                                               .cy = cy,
                                               .radius = length(circle.radius, document)},
                                   .start = {},
                                   .end = {},
                                   .construction = circle.construction});
    }
    return geometry;
}

// The profile an extrusion takes: the sketch's curves (not construction) as one closed loop of
// lines and arcs, or a single circle. Several profiles come with profile selection (#47).
std::vector<ProfileSegment> profileLoop(const SketchGeometry& sketch) {
    std::vector<const SketchCurve*> curves;
    std::size_t circles = 0;
    for (const SketchCurve& curve : sketch.curves) {
        if (!curve.construction) {
            curves.push_back(&curve);
            circles += curve.segment.kind == ProfileSegment::Kind::Circle ? 1 : 0;
        }
    }
    if (curves.empty()) {
        throw Error(ErrorCode::InvalidArgument, "The sketch has nothing to extrude");
    }
    if (circles > 0) {
        if (curves.size() == 1) {
            return {curves[0]->segment};
        }
        throw Error(ErrorCode::InvalidArgument,
                    "The sketch has several profiles; choosing one comes with profile selection");
    }
    std::map<std::string, std::vector<std::size_t>> curvesAt;
    for (std::size_t i = 0; i < curves.size(); ++i) {
        curvesAt[curves[i]->start].push_back(i);
        curvesAt[curves[i]->end].push_back(i);
    }
    for (const auto& [point, at] : curvesAt) {
        if (at.size() != 2) {
            throw Error(ErrorCode::InvalidArgument,
                        "The sketch curves must form one closed loop, but point " + point +
                            " is shared by " + std::to_string(at.size()) + " curves");
        }
    }

    std::vector<ProfileSegment> loop;
    const std::string& first = curves.front()->start;
    std::string at = first;
    std::size_t current = 0;
    while (true) {
        const SketchCurve& curve = *curves[current];
        ProfileSegment segment = curve.segment;
        if (curve.start != at) {
            // Walked from its end to its start: swap the ends; an arc then runs clockwise.
            std::swap(segment.x0, segment.x1);
            std::swap(segment.y0, segment.y1);
            segment.clockwise = !segment.clockwise;
        }
        loop.push_back(segment);
        at = curve.start == at ? curve.end : curve.start;
        if (at == first) {
            break;
        }
        const std::vector<std::size_t>& candidates = curvesAt.at(at);
        current = candidates[0] == current ? candidates[1] : candidates[0];
    }
    if (loop.size() != curves.size()) {
        throw Error(ErrorCode::InvalidArgument,
                    "The sketch curves must form one closed loop, but " +
                        std::to_string(curves.size() - loop.size()) + " curves are not part of it");
    }
    return loop;
}

// Finds a referenced element by name; if the name is gone, by its shadow (with a warning).
std::optional<std::uint32_t> resolveReference(const NamedShape& body, const Reference& reference,
                                              LengthUnit units, FeatureResult& result) {
    if (const auto id = body.find(reference.kind, reference.name)) {
        return id;
    }
    const std::string what = std::string(toString(reference.kind));
    if (reference.shadow) {
        std::vector<std::uint32_t> matches;
        const auto count = static_cast<std::uint32_t>(body.names(reference.kind).size());
        for (std::uint32_t id = 1; id <= count; ++id) {
            if (sameGeometry(elementGeometry(body.shape, reference.kind, id),
                             reference.shadow->geometry)) {
                matches.push_back(id);
            }
        }
        if (matches.size() == 1) {
            result.messages.push_back(
                "The " + what + " \"" + reference.name + "\" no longer exists; using the " + what +
                " at its last known place, a " + describe(reference.shadow->geometry, units) +
                ". Select it again to confirm.");
            return matches.front();
        }
    }
    std::string message = "The " + what + " \"" + reference.name + "\" no longer exists";
    if (reference.shadow) {
        message += "; it was a " + describe(reference.shadow->geometry, units);
    }
    result.messages.push_back(message + ".");
    return std::nullopt;
}

void executeFillet(const Feature& feature, const Fillet& fillet, const Document& document,
                   ModelState& model, FeatureResult& result) {
    const double radius = length(fillet.radius, document);
    if (fillet.edges.empty()) {
        throw Error(ErrorCode::InvalidArgument, "The fillet has no edges");
    }
    result.resolved.assign(fillet.edges.size(), std::nullopt);
    std::map<std::string, std::vector<std::uint32_t>> edgesByBody;
    for (std::size_t i = 0; i < fillet.edges.size(); ++i) {
        const Reference& edge = fillet.edges[i];
        const auto body = model.bodies.find(edge.body);
        std::optional<std::uint32_t> id;
        if (body == model.bodies.end()) {
            result.messages.push_back("The body " + edge.body + " does not exist.");
        } else {
            id = resolveReference(*body->second, edge, document.units, result);
        }
        if (!id) {
            result.brokenReferences.push_back(i);
            continue;
        }
        result.resolved[i] =
            Shadow{.id = *id, .geometry = elementGeometry(body->second->shape, edge.kind, *id)};
        std::vector<std::uint32_t>& ids = edgesByBody[edge.body];
        if (std::find(ids.begin(), ids.end(), *id) == ids.end()) {
            ids.push_back(*id);
        }
    }
    if (!result.brokenReferences.empty()) {
        throw Error(ErrorCode::InvalidArgument,
                    result.brokenReferences.size() == 1
                        ? "Select the missing edge again, or remove it from the fillet."
                        : "Select the missing edges again, or remove them from the fillet.");
    }
    for (const auto& [bodyId, ids] : edgesByBody) {
        model.bodies[bodyId] = std::make_shared<const NamedShape>(
            filletEdges(*model.bodies.at(bodyId), ids, radius, feature.id));
    }
}

// Runs one feature on `model`. On failure the model is left unchanged.
FeatureResult execute(const Feature& feature, const Document& document, ModelState& model) {
    FeatureResult result;
    result.feature = feature.id;
    if (feature.suppressed) {
        result.status = FeatureStatus::Suppressed;
        return result;
    }
    ModelState after = model;
    try {
        if (const auto* sketch = std::get_if<Sketch>(&feature.data)) {
            after.sketches[feature.id] = std::make_shared<const SketchGeometry>(
                sketchGeometry(feature, *sketch, document, model, result));
        } else if (const auto* extrude = std::get_if<Extrude>(&feature.data)) {
            const auto profile = model.sketches.find(extrude->profile);
            if (profile == model.sketches.end()) {
                throw Error(ErrorCode::InvalidArgument,
                            "The sketch " + extrude->profile + " has no profile to extrude");
            }
            after.bodies[feature.id] = std::make_shared<const NamedShape>(
                extrudeProfile(profileLoop(*profile->second), length(extrude->distance, document),
                               feature.id, profile->second->frame));
        } else if (const auto* fillet = std::get_if<Fillet>(&feature.data)) {
            executeFillet(feature, *fillet, document, after, result);
        }
    } catch (const Error& error) {
        result.status = FeatureStatus::Error;
        result.messages.emplace_back(error.what());
        return result;
    } catch (const Cancelled&) {
        throw;  // the regeneration stops; this feature is not cached
    } catch (const std::exception& error) {
        result.status = FeatureStatus::Error;
        result.messages.push_back(std::string("Internal error: ") + error.what());
        return result;
    }
    model = std::move(after);
    result.status = result.messages.empty() ? FeatureStatus::Ok : FeatureStatus::Warning;
    return result;
}

}  // namespace

Reference makeReference(const ModelState& model, std::string_view body, ElementKind kind,
                        std::uint32_t id) {
    const auto it = model.bodies.find(std::string(body));
    if (it == model.bodies.end()) {
        throw Error(ErrorCode::InvalidArgument,
                    "The body " + std::string(body) + " does not exist");
    }
    const NamedShape& shape = *it->second;
    const std::vector<std::string>& names = shape.names(kind);
    if (id == 0 || id > names.size()) {
        throw Error(ErrorCode::InvalidArgument, "The body " + std::string(body) + " has no " +
                                                    std::string(toString(kind)) + " " +
                                                    std::to_string(id));
    }
    return {.body = std::string(body),
            .kind = kind,
            .name = names[id - 1],
            .shadow = Shadow{.id = id, .geometry = elementGeometry(shape.shape, kind, id)}};
}

const FeatureResult* Regeneration::find(std::string_view feature) const noexcept {
    const auto it =
        std::find_if(features.begin(), features.end(),
                     [&](const FeatureResult& result) { return result.feature == feature; });
    return it == features.end() ? nullptr : &*it;
}

Regeneration Regenerator::run(const Document& document, const CancelToken& cancel,
                              const ProgressCallback& progress) {
    validate(document);
    const std::vector<Feature>& timeline = document.root.timeline;
    const std::size_t end = std::min(document.rollback.value_or(timeline.size()), timeline.size());

    Regeneration regeneration;
    ModelState model;
    std::size_t done = 0;
    // Long kernel operations poll the progress callback, so it can cancel mid-feature.
    const CancelScope scope(cancel, [&] {
        if (progress) {
            progress(done, end);
        }
    });
    // Cache entry i is valid while entries 0 … i - 1 matched in this run. Entries past the end of
    // the run are kept: moving the rollback marker forward again, or undoing the deletion of the
    // last feature, is then instant.
    bool cached = true;
    for (std::size_t i = 0; i < end; ++i) {
        const Feature& feature = timeline[i];
        std::string key = cacheKey(feature, document);
        if (cached && i < cache_.size() && cache_[i].key == key) {
            regeneration.features.push_back(cache_[i].result);
            regeneration.features.back().fromCache = true;
            model = cache_[i].after;
        } else {
            if (cached) {
                cache_.resize(i);
                cached = false;
            }
            if (cancel.cancelled()) {
                regeneration.cancelled = true;
                break;
            }
            FeatureResult result;
            try {
                result = execute(feature, document, model);
            } catch (const Cancelled&) {
                regeneration.cancelled = true;
                break;
            }
            ++regeneration.executed;
            cache_.push_back({.key = std::move(key), .result = result, .after = model});
            regeneration.features.push_back(std::move(result));
        }
        done = i + 1;
        if (progress) {
            progress(done, end);
        }
    }
    for (std::size_t i = regeneration.features.size(); i < timeline.size(); ++i) {
        FeatureResult skipped;
        skipped.feature = timeline[i].id;
        skipped.status = i < end ? FeatureStatus::NotRun : FeatureStatus::RolledBack;
        regeneration.features.push_back(std::move(skipped));
    }
    regeneration.model = std::move(model);
    return regeneration;
}

}  // namespace facetry::core
