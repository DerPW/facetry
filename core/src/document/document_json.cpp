// The document as JSON. Example (abridged):
//
//   {
//     "format": "facetry-document",
//     "formatVersion": 1,
//     "nextId": 12,
//     "parameters": [{"name": "width", "value": 40}],
//     "root": {"id": "root", "name": "Root", "timeline": [
//       {"id": "f1", "type": "sketch", "name": "Sketch 1", "suppressed": false, "namingVersion": 1,
//        "points": [{"id": "p2", "x": "width", "y": 0}, …],
//        "lines": [{"id": "l6", "start": "p2", "end": "p3"}, …]},
//       {"id": "f10", "type": "extrude", …, "profile": "f1", "distance": 10},
//       {"id": "f11", "type": "fillet", …, "radius": 2, "edges": [{"body": "f10", "kind": "edge",
//        "name": "edge{f10:side[f1/l6]|f10:side[f1/l7]}",
//        "shadow": {"id": 7, "type": "line", "centroid": [40, 20, 5], "size": 10}}]}
//     ]},
//     "rollback": null
//   }
//
// A number in a feature is either a literal or the name of a parameter (a string).
#include "document_json.hpp"

#include "facetry/core/error.hpp"

#include <cstdint>
#include <string>
#include <type_traits>
#include <utility>

namespace facetry::core {
namespace json {
namespace {

constexpr const char* kFormat = "facetry-document";

Json valueToJson(const Value& value) {
    return value.parameter.empty() ? Json(value.number) : Json(value.parameter);
}

Json vecToJson(const Vec3& vec) {
    return Json::array({vec.x, vec.y, vec.z});
}

Json referenceToJson(const Reference& reference) {
    Json result = {{"body", reference.body},
                   {"kind", std::string(toString(reference.kind))},
                   {"name", reference.name}};
    if (reference.shadow) {
        const Shadow& shadow = *reference.shadow;
        result["shadow"] = {{"id", shadow.id},
                            {"type", shadow.geometry.type},
                            {"centroid", vecToJson(shadow.geometry.centroid)},
                            {"size", shadow.geometry.size}};
    }
    return result;
}

ElementKind readKind(const Reader& reader) {
    const std::string kind = reader.string();
    for (const ElementKind candidate :
         {ElementKind::Face, ElementKind::Edge, ElementKind::Vertex}) {
        if (kind == toString(candidate)) {
            return candidate;
        }
    }
    invalid(reader.path() + " must be \"face\", \"edge\" or \"vertex\"");
}

Reference readReference(const Reader& reader) {
    Reference reference{.body = reader.at("body").string(),
                        .kind = readKind(reader.at("kind")),
                        .name = reader.at("name").string(),
                        .shadow = std::nullopt};
    if (reader.has("shadow")) {
        const Reader shadow = reader.at("shadow");
        const std::uint64_t id = shadow.at("id").unsignedNumber();
        if (id > UINT32_MAX) {
            invalid(shadow.path() + ".id is too large");
        }
        reference.shadow = Shadow{.id = static_cast<std::uint32_t>(id),
                                  .geometry = {.type = shadow.at("type").string(),
                                               .centroid = shadow.at("centroid").vec(),
                                               .size = shadow.at("size").number()}};
    }
    return reference;
}

FeatureData readFeatureData(const Reader& reader, const std::string& type) {
    if (type == "sketch") {
        Sketch sketch;
        for (const Reader& point : reader.at("points").items()) {
            sketch.points.push_back({.id = point.at("id").string(),
                                     .x = point.at("x").value(),
                                     .y = point.at("y").value()});
        }
        for (const Reader& line : reader.at("lines").items()) {
            sketch.lines.push_back({.id = line.at("id").string(),
                                    .start = line.at("start").string(),
                                    .end = line.at("end").string()});
        }
        return sketch;
    }
    if (type == "extrude") {
        return Extrude{.profile = reader.at("profile").string(),
                       .distance = reader.at("distance").value()};
    }
    if (type == "fillet") {
        Fillet fillet{.edges = {}, .radius = reader.at("radius").value()};
        for (const Reader& edge : reader.at("edges").items()) {
            fillet.edges.push_back(readReference(edge));
        }
        return fillet;
    }
    throw Error(ErrorCode::Unsupported, "Invalid document: " + reader.path() +
                                            " has the unknown feature type \"" + type + "\"");
}

Feature readFeature(const Reader& reader) {
    const std::string type = reader.at("type").string();
    return Feature{.id = reader.at("id").string(),
                   .name = reader.at("name").string(),
                   .suppressed = reader.at("suppressed").boolean(),
                   .namingVersion = static_cast<int>(reader.at("namingVersion").unsignedNumber()),
                   .data = readFeatureData(reader, type)};
}

}  // namespace

Json referenceJson(const Reference& reference) {
    return referenceToJson(reference);
}

Feature featureFromJson(const Json& value, const std::string& path) {
    return readFeature(Reader(value, path));
}

Json featureToJson(const Feature& feature) {
    Json result = {{"id", feature.id}};
    std::visit(
        [&](const auto& data) {
            using T = std::decay_t<decltype(data)>;
            if constexpr (std::is_same_v<T, Sketch>) {
                result["type"] = "sketch";
            } else if constexpr (std::is_same_v<T, Extrude>) {
                result["type"] = "extrude";
            } else {
                result["type"] = "fillet";
            }
        },
        feature.data);
    result["name"] = feature.name;
    result["suppressed"] = feature.suppressed;
    result["namingVersion"] = feature.namingVersion;
    if (const auto* sketch = std::get_if<Sketch>(&feature.data)) {
        Json points = Json::array();
        for (const SketchPoint& point : sketch->points) {
            points.push_back(
                {{"id", point.id}, {"x", valueToJson(point.x)}, {"y", valueToJson(point.y)}});
        }
        Json lines = Json::array();
        for (const SketchLine& line : sketch->lines) {
            lines.push_back({{"id", line.id}, {"start", line.start}, {"end", line.end}});
        }
        result["points"] = std::move(points);
        result["lines"] = std::move(lines);
    } else if (const auto* extrude = std::get_if<Extrude>(&feature.data)) {
        result["profile"] = extrude->profile;
        result["distance"] = valueToJson(extrude->distance);
    } else if (const auto* fillet = std::get_if<Fillet>(&feature.data)) {
        result["radius"] = valueToJson(fillet->radius);
        Json edges = Json::array();
        for (const Reference& edge : fillet->edges) {
            edges.push_back(referenceToJson(edge));
        }
        result["edges"] = std::move(edges);
    }
    return result;
}

}  // namespace json

std::string toJson(const Document& document) {
    using json::Json;
    Json parameters = Json::array();
    for (const Parameter& parameter : document.parameters) {
        parameters.push_back({{"name", parameter.name}, {"value", parameter.value}});
    }
    Json timeline = Json::array();
    for (const Feature& feature : document.root.timeline) {
        timeline.push_back(json::featureToJson(feature));
    }
    const Json result = {
        {"format", json::kFormat},
        {"formatVersion", kDocumentFormatVersion},
        {"nextId", document.nextId},
        {"parameters", std::move(parameters)},
        {"root",
         {{"id", document.root.id},
          {"name", document.root.name},
          {"timeline", std::move(timeline)}}},
        {"rollback", document.rollback ? Json(*document.rollback) : Json(nullptr)},
    };
    return result.dump(2) + "\n";
}

Document parseDocument(std::string_view text) {
    using json::Json;
    using json::Reader;
    Json parsed;
    try {
        parsed = Json::parse(text);
    } catch (const Json::exception& error) {
        json::invalid(std::string("not valid JSON: ") + error.what());
    }
    const Reader root(parsed, "document");
    if (root.at("format").string() != json::kFormat) {
        json::invalid("not a Facetry document");
    }
    const std::uint64_t version = root.at("formatVersion").unsignedNumber();
    if (version > static_cast<std::uint64_t>(kDocumentFormatVersion)) {
        throw Error(ErrorCode::Unsupported,
                    "This document was saved by a newer version of "
                    "Facetry (format version " +
                        std::to_string(version) + ")");
    }
    if (version < 1) {
        json::invalid("format version 0 does not exist");
    }

    Document document;
    document.nextId = root.at("nextId").unsignedNumber();
    for (const Reader& parameter : root.at("parameters").items()) {
        document.parameters.push_back(
            {.name = parameter.at("name").string(), .value = parameter.at("value").number()});
    }
    const Reader component = root.at("root");
    document.root.id = component.at("id").string();
    document.root.name = component.at("name").string();
    for (const Reader& feature : component.at("timeline").items()) {
        document.root.timeline.push_back(json::readFeature(feature));
    }
    if (root.has("rollback")) {
        document.rollback = static_cast<std::size_t>(root.at("rollback").unsignedNumber());
    }
    validate(document);
    return document;
}

}  // namespace facetry::core
