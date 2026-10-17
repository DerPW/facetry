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

double resolve(const Value& value, const Document& document) {
    if (value.parameter.empty()) {
        return value.number;
    }
    const Parameter* parameter = document.findParameter(value.parameter);
    if (parameter == nullptr) {
        throw Error(ErrorCode::InvalidArgument,
                    "The parameter \"" + value.parameter + "\" does not exist");
    }
    return parameter->value;
}

// What a feature's result depends on, besides the features before it: its definition with
// parameter values filled in. Names and shadows do not change the result.
std::string cacheKey(Feature feature, const Document& document) {
    const auto fill = [&](Value& value) {
        if (const Parameter* parameter = document.findParameter(value.parameter)) {
            value = Value(parameter->value);
        }
    };
    if (auto* sketch = std::get_if<Sketch>(&feature.data)) {
        for (SketchPoint& point : sketch->points) {
            fill(point.x);
            fill(point.y);
        }
    } else if (auto* extrude = std::get_if<Extrude>(&feature.data)) {
        fill(extrude->distance);
    } else if (auto* fillet = std::get_if<Fillet>(&feature.data)) {
        fill(fillet->radius);
        for (Reference& edge : fillet->edges) {
            edge.shadow.reset();
        }
    }
    feature.name.clear();
    return json::featureToJson(feature).dump();
}

std::string describe(const ElementGeometry& geometry) {
    std::ostringstream text;
    text << std::setprecision(6) << geometry.type << " at (" << geometry.centroid.x << ", "
         << geometry.centroid.y << ", " << geometry.centroid.z << ")";
    return text.str();
}

// The sketch's lines, walked as one closed loop starting with the first line.
std::vector<ProfileSegment> sketchProfile(const Feature& feature, const Sketch& sketch,
                                          const Document& document) {
    if (sketch.lines.empty()) {
        throw Error(ErrorCode::InvalidArgument, "The sketch has no lines");
    }
    std::map<std::string, std::pair<double, double>> points;
    for (const SketchPoint& point : sketch.points) {
        points[point.id] = {resolve(point.x, document), resolve(point.y, document)};
    }
    std::map<std::string, std::vector<std::size_t>> linesAt;
    for (std::size_t i = 0; i < sketch.lines.size(); ++i) {
        linesAt[sketch.lines[i].start].push_back(i);
        linesAt[sketch.lines[i].end].push_back(i);
    }
    for (const auto& [point, lines] : linesAt) {
        if (lines.size() != 2) {
            throw Error(ErrorCode::InvalidArgument,
                        "The sketch lines must form one closed loop, but point " + point +
                            " is shared by " + std::to_string(lines.size()) + " lines");
        }
    }

    std::vector<ProfileSegment> loop;
    const std::string& first = sketch.lines.front().start;
    std::string at = first;
    std::size_t current = 0;
    while (true) {
        const SketchLine& line = sketch.lines[current];
        const std::string& next = line.start == at ? line.end : line.start;
        const auto [x0, y0] = points.at(at);
        const auto [x1, y1] = points.at(next);
        loop.push_back({feature.id + "/" + line.id, x0, y0, x1, y1});
        at = next;
        if (at == first) {
            break;
        }
        const std::vector<std::size_t>& candidates = linesAt.at(at);
        current = candidates[0] == current ? candidates[1] : candidates[0];
    }
    if (loop.size() != sketch.lines.size()) {
        throw Error(ErrorCode::InvalidArgument,
                    "The sketch lines must form one closed loop, but " +
                        std::to_string(sketch.lines.size() - loop.size()) +
                        " lines are not part of it");
    }
    return loop;
}

// Finds a referenced element by name; if the name is gone, by its shadow (with a warning).
std::optional<std::uint32_t> resolveReference(const NamedShape& body, const Reference& reference,
                                              FeatureResult& result) {
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
                " at its last known place, a " + describe(reference.shadow->geometry) +
                ". Select it again to confirm.");
            return matches.front();
        }
    }
    std::string message = "The " + what + " \"" + reference.name + "\" no longer exists";
    if (reference.shadow) {
        message += "; it was a " + describe(reference.shadow->geometry);
    }
    result.messages.push_back(message + ".");
    return std::nullopt;
}

void executeFillet(const Feature& feature, const Fillet& fillet, const Document& document,
                   ModelState& model, FeatureResult& result) {
    const double radius = resolve(fillet.radius, document);
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
            id = resolveReference(*body->second, edge, result);
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
            after.profiles[feature.id] = std::make_shared<const std::vector<ProfileSegment>>(
                sketchProfile(feature, *sketch, document));
        } else if (const auto* extrude = std::get_if<Extrude>(&feature.data)) {
            const auto profile = model.profiles.find(extrude->profile);
            if (profile == model.profiles.end()) {
                throw Error(ErrorCode::InvalidArgument,
                            "The sketch " + extrude->profile + " has no profile to extrude");
            }
            after.bodies[feature.id] = std::make_shared<const NamedShape>(
                extrudeProfile(*profile->second, resolve(extrude->distance, document), feature.id));
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
