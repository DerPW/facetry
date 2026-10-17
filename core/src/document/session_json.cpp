#include "facetry/core/session_json.hpp"

#include "facetry/core/error.hpp"
#include "facetry/core/sketch_edit.hpp"

#include "document_json.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace facetry::core {
namespace {

using json::Json;
using json::Reader;

[[noreturn]] void invalidEdit(const std::string& message) {
    throw Error(ErrorCode::InvalidArgument, "Invalid edit: " + message);
}

std::string typeName(const FeatureData& data) {
    if (std::holds_alternative<Sketch>(data)) {
        return "sketch";
    }
    if (std::holds_alternative<Extrude>(data)) {
        return "extrude";
    }
    return "fillet";
}

// "Sketch 2" for the second sketch, and so on.
std::string defaultName(const Document& document, const std::string& type) {
    const auto count =
        std::count_if(document.root.timeline.begin(), document.root.timeline.end(),
                      [&](const Feature& feature) { return typeName(feature.data) == type; });
    std::string name = type;
    if (!name.empty()) {
        name[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(name[0])));
    }
    return name + " " + std::to_string(count + 1);
}

std::vector<Feature>::iterator requireFeature(Document& document, const std::string& id) {
    auto& timeline = document.root.timeline;
    const auto it = std::find_if(timeline.begin(), timeline.end(),
                                 [&](const Feature& feature) { return feature.id == id; });
    if (it == timeline.end()) {
        invalidEdit("the feature " + id + " does not exist");
    }
    return it;
}

std::vector<Parameter>::iterator findParameter(Document& document, const std::string& name) {
    return std::find_if(document.parameters.begin(), document.parameters.end(),
                        [&](const Parameter& parameter) { return parameter.name == name; });
}

// Reads a feature from an edit, filling in what a new feature may leave out, or what a replaced
// feature keeps from `existing`.
Feature readFeature(Document& document, const Json& value, const std::string& path,
                    const Feature* existing) {
    if (!value.is_object()) {
        invalidEdit(path + " must be an object");
    }
    Json filled = value;
    const std::string type = Reader(value, path).at("type").string();
    if (!filled.contains("id")) {
        filled["id"] = document.newId("f");
    }
    if (!filled.contains("name")) {
        filled["name"] = existing != nullptr ? existing->name : defaultName(document, type);
    }
    if (!filled.contains("suppressed")) {
        filled["suppressed"] = existing != nullptr && existing->suppressed;
    }
    if (!filled.contains("namingVersion")) {
        filled["namingVersion"] =
            static_cast<unsigned>(existing != nullptr ? existing->namingVersion : kNamingVersion);
    }
    return json::featureFromJson(filled, path);
}

void applyEdit(Document& document, const Json& edit, const std::string& path) {
    const Reader reader(edit, path);
    const std::string op = reader.at("op").string();
    auto& timeline = document.root.timeline;

    if (op == "setParameter" || op == "addParameter" || op == "deleteParameter") {
        const std::string name = reader.at("name").string();
        const auto parameter = findParameter(document, name);
        const bool exists = parameter != document.parameters.end();
        if (op == "addParameter") {
            if (exists) {
                invalidEdit("the parameter " + name + " already exists");
            }
            document.parameters.push_back({name, reader.at("value").number()});
            return;
        }
        if (!exists) {
            invalidEdit("the parameter " + name + " does not exist");
        }
        if (op == "setParameter") {
            parameter->value = reader.at("value").number();
        } else {
            document.parameters.erase(parameter);
        }
        return;
    }

    if (op == "addFeature") {
        Feature feature = readFeature(document, edit.at("feature"), path + ".feature", nullptr);
        std::size_t index = document.rollback.value_or(timeline.size());
        if (reader.has("index")) {
            index = static_cast<std::size_t>(reader.at("index").unsignedNumber());
            if (index > timeline.size()) {
                invalidEdit("index " + std::to_string(index) + " is past the end of the timeline");
            }
        }
        // A feature inserted before the rollback marker is active: the marker moves past it.
        if (document.rollback && index <= *document.rollback) {
            ++*document.rollback;
        }
        timeline.insert(timeline.begin() + static_cast<std::ptrdiff_t>(index), std::move(feature));
        return;
    }
    if (op == "replaceFeature") {
        if (!edit.contains("feature") || !edit.at("feature").is_object() ||
            !edit.at("feature").contains("id")) {
            invalidEdit(path + ".feature needs the \"id\" of the feature to replace");
        }
        const auto it = requireFeature(
            document, Reader(edit.at("feature"), path + ".feature").at("id").string());
        *it = readFeature(document, edit.at("feature"), path + ".feature", &*it);
        return;
    }
    if (op == "deleteFeature" || op == "renameFeature" || op == "suppressFeature") {
        const auto it = requireFeature(document, reader.at("id").string());
        if (op == "renameFeature") {
            it->name = reader.at("name").string();
        } else if (op == "suppressFeature") {
            it->suppressed = reader.at("suppressed").boolean();
        } else {
            const auto index = static_cast<std::size_t>(it - timeline.begin());
            timeline.erase(it);
            if (document.rollback && index < *document.rollback) {
                --*document.rollback;
            }
        }
        return;
    }
    if (op == "renameBody" || op == "setBodyVisible" || op == "setBodyOpacity") {
        const std::string id = reader.at("id").string();
        const auto bodies = document.bodyFeatures();
        if (std::none_of(bodies.begin(), bodies.end(),
                         [&](const Feature* feature) { return feature->id == id; })) {
            invalidEdit("the body " + id + " does not exist");
        }
        auto& settings = document.root.bodies;
        auto it = std::find_if(settings.begin(), settings.end(),
                               [&](const BodySettings& body) { return body.id == id; });
        if (it == settings.end()) {
            settings.push_back({.id = id, .name = "", .visible = true, .opacity = 1.0});
            it = std::prev(settings.end());
        }
        if (op == "renameBody") {
            it->name = reader.at("name").string();
        } else if (op == "setBodyVisible") {
            it->visible = reader.at("visible").boolean();
        } else {
            const double opacity = reader.at("opacity").number();
            if (!(opacity >= 0.0 && opacity <= 1.0)) {
                invalidEdit(path + ".opacity must be between 0 and 1");
            }
            it->opacity = opacity;
        }
        // Back to the defaults: drop the entry, so the document stays as it was.
        if (it->name.empty() && it->visible && it->opacity == 1.0) {
            settings.erase(it);
        }
        return;
    }
    if (op == "setUnits") {
        const Reader units = reader.at("units");
        const auto unit = unitFromSymbol(units.string());
        if (!unit) {
            invalidEdit(path + ".units: unknown unit \"" + units.string() + "\"");
        }
        convertUnits(document, *unit);
        return;
    }
    if (op == "setOriginVisible") {
        const std::string id = reader.at("id").string();
        auto& visible = document.root.visibleOrigin;
        visible.erase(std::remove(visible.begin(), visible.end(), id), visible.end());
        if (reader.at("visible").boolean()) {
            visible.push_back(id);
        }
        return;
    }
    if (op == "deleteSketchEntities") {
        const std::string sketch = reader.at("sketch").string();
        const Feature* feature = document.findFeature(sketch);
        if (feature == nullptr || !std::holds_alternative<Sketch>(feature->data)) {
            invalidEdit(path + ": \"sketch\" is not a sketch of the document");
        }
        std::vector<std::string> entities;
        for (const Reader& entity : reader.at("entities").items()) {
            entities.push_back(entity.string());
        }
        try {
            deleteSketchEntities(document, sketch, entities);
        } catch (const Error& error) {
            invalidEdit(path + ": " + error.what());
        }
        return;
    }
    if (op == "setConstruction") {
        Feature* feature = document.findFeature(reader.at("sketch").string());
        auto* sketch = feature != nullptr ? std::get_if<Sketch>(&feature->data) : nullptr;
        if (sketch == nullptr) {
            invalidEdit(path + ": \"sketch\" is not a sketch of the document");
        }
        const bool construction = reader.at("construction").boolean();
        for (const Reader& entity : reader.at("entities").items()) {
            const std::string id = entity.string();
            const auto mark = [&](auto& curves) {
                for (auto& curve : curves) {
                    if (curve.id == id) {
                        curve.construction = construction;
                        return true;
                    }
                }
                return false;
            };
            if (!mark(sketch->lines) && !mark(sketch->arcs) && !mark(sketch->circles)) {
                invalidEdit(path + ": " + id + " is not a curve of sketch " + feature->id);
            }
        }
        return;
    }
    if (op == "setRollback") {
        if (!edit.contains("index")) {
            invalidEdit(path + ": missing \"index\"");
        }
        if (edit.at("index").is_null()) {
            document.rollback.reset();
        } else {
            document.rollback = static_cast<std::size_t>(reader.at("index").unsignedNumber());
        }
        return;
    }
    invalidEdit(path + " has the unknown op \"" + op + "\"");
}

// IDs that look like feature IDs inside a reference name, e.g. "f2" in "edge{f2:side[f1/l2]}":
// an identifier followed by ':' (a feature's role) or '/' (a sketch element).
void collectIds(std::string_view name, std::set<std::string>& ids) {
    std::size_t start = std::string_view::npos;
    for (std::size_t i = 0; i <= name.size(); ++i) {
        const char c = i < name.size() ? name[i] : '\0';
        if (std::isalnum(static_cast<unsigned char>(c)) != 0) {
            if (start == std::string_view::npos) {
                start = i;
            }
            continue;
        }
        if (start != std::string_view::npos && (c == ':' || c == '/')) {
            ids.insert(std::string(name.substr(start, i - start)));
        }
        start = std::string_view::npos;
    }
}

// The features a feature uses directly: its profile, the bodies it changes, and the features
// whose results its references name.
std::set<std::string> dependenciesOf(const Feature& feature) {
    std::set<std::string> ids;
    if (const auto* extrude = std::get_if<Extrude>(&feature.data)) {
        ids.insert(extrude->profile);
    }
    if (const auto* references = feature.references()) {
        for (const Reference& reference : *references) {
            ids.insert(reference.body);
            collectIds(reference.name, ids);
        }
    }
    ids.erase(feature.id);
    return ids;
}

// For each feature, the later features that depend on it, directly or through others: what
// breaks when it is deleted.
std::map<std::string, std::vector<std::string>> dependents(const std::vector<Feature>& timeline) {
    std::map<std::string, std::vector<std::string>> result;
    std::vector<std::set<std::string>> dependencies;
    for (const Feature& feature : timeline) {
        dependencies.push_back(dependenciesOf(feature));
    }
    for (std::size_t i = 0; i < timeline.size(); ++i) {
        std::set<std::string> affected{timeline[i].id};
        auto& list = result[timeline[i].id];
        for (std::size_t j = i + 1; j < timeline.size(); ++j) {
            const bool depends =
                std::any_of(dependencies[j].begin(), dependencies[j].end(),
                            [&](const std::string& id) { return affected.contains(id); });
            if (depends) {
                affected.insert(timeline[j].id);
                list.push_back(timeline[j].id);
            }
        }
    }
    return result;
}

std::string stepName(const Json& edit) {
    if (!edit.is_object() || !edit.contains("op") || !edit["op"].is_string()) {
        return "Edit";
    }
    const std::string op = edit["op"].get<std::string>();
    const auto flag = [&](const char* key) {
        return edit.contains(key) && edit[key].is_boolean() && edit[key].get<bool>();
    };
    if (op == "setParameter" && edit.contains("name") && edit["name"].is_string()) {
        return "Change " + edit["name"].get<std::string>();
    }
    if (op == "addParameter")
        return "Add parameter";
    if (op == "deleteParameter")
        return "Delete parameter";
    if (op == "addFeature")
        return "Add feature";
    if (op == "replaceFeature")
        return "Edit feature";
    if (op == "deleteFeature")
        return "Delete feature";
    if (op == "renameFeature")
        return "Rename feature";
    if (op == "suppressFeature")
        return flag("suppressed") ? "Suppress feature" : "Unsuppress feature";
    if (op == "setRollback")
        return "Move rollback marker";
    if (op == "renameBody")
        return "Rename body";
    if (op == "setBodyVisible")
        return flag("visible") ? "Show body" : "Hide body";
    if (op == "setBodyOpacity")
        return "Change body opacity";
    if (op == "setOriginVisible")
        return flag("visible") ? "Show origin item" : "Hide origin item";
    if (op == "setUnits")
        return "Change units";
    if (op == "setConstruction")
        return flag("construction") ? "Make construction" : "Make normal";
    if (op == "deleteSketchEntities")
        return "Delete sketch entities";
    return "Edit";
}

const char* statusName(FeatureStatus status) {
    switch (status) {
        case FeatureStatus::Ok:
            return "ok";
        case FeatureStatus::Warning:
            return "warning";
        case FeatureStatus::Error:
            return "error";
        case FeatureStatus::Suppressed:
            return "suppressed";
        case FeatureStatus::RolledBack:
            return "rolledBack";
        case FeatureStatus::NotRun:
            break;
    }
    return "notRun";
}

}  // namespace

void applyEdits(Document& document, std::string_view text) {
    Json parsed;
    try {
        parsed = Json::parse(text);
    } catch (const Json::exception& error) {
        invalidEdit(std::string("not valid JSON: ") + error.what());
    }
    if (parsed.is_array()) {
        for (std::size_t i = 0; i < parsed.size(); ++i) {
            applyEdit(document, parsed.at(i), "edits[" + std::to_string(i) + "]");
        }
    } else {
        applyEdit(document, parsed, "edit");
    }
}

std::string editName(std::string_view text) {
    const Json parsed = Json::parse(text, nullptr, false);
    if (parsed.is_array()) {
        return parsed.empty() ? "Edit" : stepName(parsed.front());
    }
    return stepName(parsed);
}

std::string sessionStateJson(const Session& session) {
    const Document& document = session.document();
    const Regeneration& regeneration = session.regeneration();

    Json parameters = Json::array();
    for (const Parameter& parameter : document.parameters) {
        parameters.push_back({{"name", parameter.name}, {"value", parameter.value}});
    }
    Json features = Json::array();
    const auto dependentsById = dependents(document.root.timeline);
    for (const Feature& feature : document.root.timeline) {
        Json entry = {{"id", feature.id},
                      {"name", feature.name},
                      {"type", typeName(feature.data)},
                      {"suppressed", feature.suppressed}};
        const FeatureResult* result = regeneration.find(feature.id);
        entry["status"] = statusName(result != nullptr ? result->status : FeatureStatus::NotRun);
        entry["messages"] = result != nullptr ? result->messages : std::vector<std::string>{};
        entry["brokenReferences"] =
            result != nullptr ? result->brokenReferences : std::vector<std::size_t>{};
        entry["dependents"] = dependentsById.at(feature.id);
        if (std::holds_alternative<Sketch>(feature.data)) {
            Json regions = Json::array();
            Json frame = nullptr;
            Json glyphs = Json::array();
            if (const auto sketch = regeneration.model.sketches.find(feature.id);
                sketch != regeneration.model.sketches.end()) {
                for (const SketchRegion& region : sketch->second->regions) {
                    regions.push_back(region.name);
                }
                const SketchFrame& f = sketch->second->frame;
                const auto vector = [](const Vec3& v) { return Json::array({v.x, v.y, v.z}); };
                frame = {{"origin", vector(f.origin)},
                         {"xAxis", vector(f.xAxis)},
                         {"yAxis", vector(f.yAxis)},
                         {"normal", vector(f.normal())}};
                for (const ConstraintGlyph& glyph :
                     constraintGlyphs(std::get<Sketch>(feature.data), *sketch->second)) {
                    glyphs.push_back({{"type", glyph.type},
                                      {"constraint", glyph.constraint},
                                      {"at", vector(glyph.at)}});
                }
            }
            entry["regions"] = std::move(regions);
            entry["frame"] = std::move(frame);
            entry["glyphs"] = std::move(glyphs);
        }
        entry["definition"] = json::featureToJson(feature);
        features.push_back(std::move(entry));
    }
    Json bodies = Json::array();
    for (const Feature* feature : document.bodyFeatures()) {
        if (regeneration.model.bodies.contains(feature->id)) {
            bodies.push_back({{"id", feature->id},
                              {"name", document.bodyName(feature->id)},
                              {"visible", document.bodyVisible(feature->id)},
                              {"opacity", document.bodyOpacity(feature->id)}});
        }
    }
    Json origin = Json::array();
    for (const OriginItem& item : originItems()) {
        const auto& visible = document.root.visibleOrigin;
        origin.push_back(
            {{"id", item.id},
             {"name", item.name},
             {"kind", item.kind},
             {"visible", std::find(visible.begin(), visible.end(), item.id) != visible.end()}});
    }
    const Json state = {
        {"units", unitSymbol(document.units)},
        {"parameters", std::move(parameters)},
        {"rollback", document.rollback ? Json(*document.rollback) : Json(nullptr)},
        {"canUndo", session.canUndo()},
        {"canRedo", session.canRedo()},
        {"undoName", session.canUndo() ? Json(std::string(session.undoName())) : Json(nullptr)},
        {"redoName", session.canRedo() ? Json(std::string(session.redoName())) : Json(nullptr)},
        {"inTransaction", session.inTransaction()},
        {"undoCount", session.undoCount()},
        {"needsRegeneration", session.needsRegeneration()},
        {"features", std::move(features)},
        {"bodies", std::move(bodies)},
        {"origin", std::move(origin)},
        {"regeneration",
         {{"cancelled", regeneration.cancelled}, {"executed", regeneration.executed}}},
    };
    return state.dump();
}

std::string elementNamesJson(const NamedShape& shape) {
    return Json{{"faces", shape.faces}, {"edges", shape.edges}, {"vertices", shape.vertices}}
        .dump();
}

std::string referenceJson(const Reference& reference) {
    return json::referenceJson(reference).dump();
}

}  // namespace facetry::core
