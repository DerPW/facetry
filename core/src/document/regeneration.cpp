#include "facetry/core/regeneration.hpp"

#include "facetry/core/error.hpp"

#include "document_json.hpp"

#include <algorithm>
#include <cmath>
#include <exception>
#include <iomanip>
#include <map>
#include <numbers>
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
    SketchGeometry geometry{.frame = sketchFrame(sketch, document, model, result),
                            .curves = {},
                            .regions = {},
                            .points = {}};
    std::map<std::string, std::pair<double, double>> points;
    for (const SketchPoint& point : sketch.points) {
        const double x = length(point.x, document);
        const double y = length(point.y, document);
        points[point.id] = {x, y};
        geometry.points.push_back(geometry.frame.at(x, y));
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
    std::vector<ProfileSegment> bounding;
    for (const SketchCurve& curve : geometry.curves) {
        if (!curve.construction) {
            bounding.push_back(curve.segment);
        }
    }
    geometry.regions = sketchRegions(bounding, geometry.frame);
    return geometry;
}

}  // namespace

Mesh sketchMesh(const SketchGeometry& sketch, double deflection) {
    std::vector<ProfileSegment> curves;
    for (const SketchCurve& curve : sketch.curves) {
        curves.push_back(curve.segment);
    }
    return curvesMesh(sketch.frame, curves, sketch.points, deflection);
}

Mesh curvesMesh(const SketchFrame& frame, std::span<const ProfileSegment> curves,
                std::span<const Vec3> points, double deflection) {
    Mesh mesh;
    const auto add = [&](double x, double y) {
        const Vec3 p = frame.at(x, y);
        mesh.edgePoints.insert(
            mesh.edgePoints.end(),
            {static_cast<float>(p.x), static_cast<float>(p.y), static_cast<float>(p.z)});
    };
    for (std::size_t i = 0; i < curves.size(); ++i) {
        const ProfileSegment& curve = curves[i];
        if (curve.kind == ProfileSegment::Kind::Line) {
            add(curve.x0, curve.y0);
            add(curve.x1, curve.y1);
        } else {
            const bool circle = curve.kind == ProfileSegment::Kind::Circle;
            const double radius =
                circle ? curve.radius : std::hypot(curve.x0 - curve.cx, curve.y0 - curve.cy);
            const double start =
                circle ? 0.0 : std::atan2(curve.y0 - curve.cy, curve.x0 - curve.cx);
            double sweep = 2.0 * std::numbers::pi;
            if (!circle) {
                const double end = std::atan2(curve.y1 - curve.cy, curve.x1 - curve.cx);
                sweep =
                    std::fmod(curve.clockwise ? start - end : end - start, 2.0 * std::numbers::pi);
                if (sweep <= 0.0) {
                    sweep += 2.0 * std::numbers::pi;
                }
            }
            // Steps short enough to stay within the deflection, and at most 0.2 rad.
            const double ratio =
                radius > 0.0 ? std::clamp(1.0 - deflection / radius, -1.0, 1.0) : -1.0;
            const double step = std::clamp(2.0 * std::acos(ratio), 1e-3, 0.2);
            const auto count =
                static_cast<std::size_t>(std::clamp(std::ceil(sweep / step), 2.0, 512.0));
            const double direction = curve.clockwise ? -1.0 : 1.0;
            for (std::size_t k = 0; k <= count; ++k) {
                const double angle =
                    start + direction * sweep * static_cast<double>(k) / static_cast<double>(count);
                add(curve.cx + radius * std::cos(angle), curve.cy + radius * std::sin(angle));
            }
        }
        mesh.edgeOffsets.push_back(static_cast<std::uint32_t>(mesh.edgePoints.size() / 3));
        mesh.edgeIds.push_back(static_cast<std::uint32_t>(i + 1));
    }
    for (std::size_t i = 0; i < points.size(); ++i) {
        const Vec3& p = points[i];
        mesh.vertexPositions.insert(
            mesh.vertexPositions.end(),
            {static_cast<float>(p.x), static_cast<float>(p.y), static_cast<float>(p.z)});
        mesh.vertexIds.push_back(static_cast<std::uint32_t>(i + 1));
    }
    return mesh;
}

std::vector<ConstraintGlyph> constraintGlyphs(const Sketch& sketch,
                                              const SketchGeometry& geometry) {
    // Where each point and curve shows a glyph, in sketch coordinates: points where they are,
    // curves halfway along (a circle at its top).
    std::map<std::string, std::pair<double, double>> places;
    const Vec3 origin = geometry.frame.origin;
    const auto inPlane = [&](const Vec3& p) {
        const Vec3 d{p.x - origin.x, p.y - origin.y, p.z - origin.z};
        const Vec3& x = geometry.frame.xAxis;
        const Vec3& y = geometry.frame.yAxis;
        return std::pair{d.x * x.x + d.y * x.y + d.z * x.z, d.x * y.x + d.y * y.y + d.z * y.z};
    };
    for (std::size_t i = 0; i < sketch.points.size() && i < geometry.points.size(); ++i) {
        places[sketch.points[i].id] = inPlane(geometry.points[i]);
    }
    std::map<std::string, const SketchCurve*> curves;
    for (const SketchCurve& curve : geometry.curves) {
        const std::string& name = curve.segment.name;
        const std::string id = name.substr(name.find('/') + 1);
        curves[id] = &curve;
        const ProfileSegment& s = curve.segment;
        switch (s.kind) {
            case ProfileSegment::Kind::Line:
                places[id] = {(s.x0 + s.x1) / 2, (s.y0 + s.y1) / 2};
                break;
            case ProfileSegment::Kind::Arc: {
                const double radius = std::hypot(s.x0 - s.cx, s.y0 - s.cy);
                const double start = std::atan2(s.y0 - s.cy, s.x0 - s.cx);
                double sweep = std::atan2(s.y1 - s.cy, s.x1 - s.cx) - start;
                while (sweep <= 0.0) {
                    sweep += 2.0 * std::numbers::pi;
                }
                places[id] = {s.cx + radius * std::cos(start + sweep / 2),
                              s.cy + radius * std::sin(start + sweep / 2)};
                break;
            }
            case ProfileSegment::Kind::Circle:
                places[id] = {s.cx, s.cy + s.radius};
                break;
        }
    }
    std::vector<ConstraintGlyph> glyphs;
    const auto add = [&](const SketchConstraint& constraint, const std::pair<double, double>& at) {
        glyphs.push_back({constraint.type, constraint.id, geometry.frame.at(at.first, at.second)});
    };
    for (const SketchConstraint& constraint : sketch.constraints) {
        std::vector<std::pair<double, double>> at;
        for (const std::string& entity : constraint.entities) {
            const auto place = places.find(entity);
            if (place == places.end()) {
                break;
            }
            at.push_back(place->second);
        }
        if (at.size() != constraint.entities.size() || at.empty()) {
            continue;
        }
        const std::string& type = constraint.type;
        if (type == "parallel" || type == "perpendicular" || type == "equal") {
            for (const auto& place : at) {
                add(constraint, place);
            }
        } else if (type == "horizontal" || type == "vertical") {
            // A line, or halfway between two points.
            add(constraint, at.size() == 2 ? std::pair{(at[0].first + at[1].first) / 2,
                                                       (at[0].second + at[1].second) / 2}
                                           : at[0]);
        } else if (type == "tangent" && at.size() == 2) {
            // Where the curves meet, if they share an end; else on the second.
            const SketchCurve* a =
                curves.contains(constraint.entities[0]) ? curves[constraint.entities[0]] : nullptr;
            const SketchCurve* b =
                curves.contains(constraint.entities[1]) ? curves[constraint.entities[1]] : nullptr;
            std::string shared;
            if (a != nullptr && b != nullptr) {
                for (const std::string& end : {a->start, a->end}) {
                    if (!end.empty() && (end == b->start || end == b->end)) {
                        shared = end;
                    }
                }
            }
            add(constraint, places.contains(shared) ? places[shared] : at[1]);
        } else {
            add(constraint, at[0]);  // the point of coincident, midpoint and fix; a center
        }
    }
    return glyphs;
}

double lengthInMillimeters(const Value& value, const Document& document) {
    return length(value, document);
}

namespace {

// The regions an extrusion takes: the ones it names, or the sketch's only region.
std::vector<SketchRegion> chosenRegions(const Extrude& extrude, const SketchGeometry& sketch) {
    if (extrude.regions.empty()) {
        if (sketch.regions.empty()) {
            throw Error(ErrorCode::InvalidArgument,
                        "The sketch has no closed profile: its curves enclose no region");
        }
        if (sketch.regions.size() > 1) {
            throw Error(ErrorCode::InvalidArgument,
                        "The sketch has several profiles; choose the ones to extrude");
        }
        return sketch.regions;
    }
    std::vector<SketchRegion> chosen;
    for (const std::string& name : extrude.regions) {
        const auto region =
            std::find_if(sketch.regions.begin(), sketch.regions.end(),
                         [&](const SketchRegion& candidate) { return candidate.name == name; });
        if (region == sketch.regions.end()) {
            throw Error(ErrorCode::InvalidArgument,
                        "A chosen profile is no longer in the sketch (" + name +
                            "); choose the profiles again");
        }
        chosen.push_back(*region);
    }
    return chosen;
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
                extrudeRegions(chosenRegions(*extrude, *profile->second), profile->second->frame,
                               length(extrude->distance, document), feature.id));
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
