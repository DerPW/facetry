#include "facetry/core/session_json.hpp"

#include "facetry/core/error.hpp"

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
    if (op == "renameBody" || op == "setBodyVisible") {
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
            settings.push_back({.id = id, .name = "", .visible = true});
            it = std::prev(settings.end());
        }
        if (op == "renameBody") {
            it->name = reader.at("name").string();
        } else {
            it->visible = reader.at("visible").boolean();
        }
        // Back to the defaults: drop the entry, so the document stays as it was.
        if (it->name.empty() && it->visible) {
            settings.erase(it);
        }
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
        entry["definition"] = json::featureToJson(feature);
        features.push_back(std::move(entry));
    }
    Json bodies = Json::array();
    for (const Feature* feature : document.bodyFeatures()) {
        if (regeneration.model.bodies.contains(feature->id)) {
            bodies.push_back({{"id", feature->id},
                              {"name", document.bodyName(feature->id)},
                              {"visible", document.bodyVisible(feature->id)}});
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
        {"parameters", std::move(parameters)},
        {"rollback", document.rollback ? Json(*document.rollback) : Json(nullptr)},
        {"canUndo", session.canUndo()},
        {"canRedo", session.canRedo()},
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
