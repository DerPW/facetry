// The document as JSON. Example (abridged):
//
//   {
//     "format": "facetry-document",
//     "formatVersion": 3,
//     "nextId": 12,
//     "parameters": [{"name": "width", "value": 40}],
//     "root": {"id": "root", "name": "Root", "timeline": [
//       {"id": "f1", "type": "sketch", "name": "Sketch 1", "suppressed": false, "namingVersion": 1,
//        "plane": "xy", "points": [{"id": "p2", "x": "width", "y": 0}, …],
//        "lines": [{"id": "l6", "start": "p2", "end": "p3"}, …],
//        "circles": [{"id": "c9", "center": "p8", "radius": 3, "construction": true}]},
//       {"id": "f10", "type": "extrude", …, "profile": "f1", "distance": 10,
//        "regions": ["region{f1/l6+|f1/l7+|f1/l8+|f1/l9+}"]},
//       {"id": "f11", "type": "fillet", …, "radius": 2, "edges": [{"body": "f10", "kind": "edge",
//        "name": "edge{f10:side[f1/l6]|f10:side[f1/l7]}",
//        "shadow": {"id": 7, "type": "line", "centroid": [40, 20, 5], "size": 10}}]}
//     ]},
//     "rollback": null,
//     "units": "mm"
//   }
//
// A number in a feature is either a literal or the name of a parameter (a string). Lengths are in
// the document's units; shadows are kernel geometry and always in millimeters.
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
        // Format version 2 and older: no plane (XY), only points and lines.
        if (reader.has("plane")) {
            sketch.plane = reader.at("plane").string();
        }
        if (reader.has("face")) {
            sketch.face.push_back(readReference(reader.at("face")));
        }
        const auto construction = [](const Reader& curve) {
            return curve.has("construction") && curve.at("construction").boolean();
        };
        for (const Reader& point : reader.at("points").items()) {
            sketch.points.push_back({.id = point.at("id").string(),
                                     .x = point.at("x").value(),
                                     .y = point.at("y").value()});
        }
        for (const Reader& line : reader.at("lines").items()) {
            sketch.lines.push_back({.id = line.at("id").string(),
                                    .start = line.at("start").string(),
                                    .end = line.at("end").string(),
                                    .construction = construction(line)});
        }
        if (reader.has("arcs")) {
            for (const Reader& arc : reader.at("arcs").items()) {
                sketch.arcs.push_back({.id = arc.at("id").string(),
                                       .center = arc.at("center").string(),
                                       .start = arc.at("start").string(),
                                       .end = arc.at("end").string(),
                                       .construction = construction(arc)});
            }
        }
        if (reader.has("circles")) {
            for (const Reader& circle : reader.at("circles").items()) {
                sketch.circles.push_back({.id = circle.at("id").string(),
                                          .center = circle.at("center").string(),
                                          .radius = circle.at("radius").value(),
                                          .construction = construction(circle)});
            }
        }
        if (reader.has("constraints")) {
            for (const Reader& constraint : reader.at("constraints").items()) {
                SketchConstraint entry{.id = constraint.at("id").string(),
                                       .type = constraint.at("type").string(),
                                       .entities = {}};
                for (const Reader& entity : constraint.at("entities").items()) {
                    entry.entities.push_back(entity.string());
                }
                sketch.constraints.push_back(std::move(entry));
            }
        }
        return sketch;
    }
    if (type == "extrude") {
        Extrude extrude{.profile = reader.at("profile").string(),
                        .distance = reader.at("distance").value(),
                        .regions = {}};
        if (reader.has("regions")) {
            for (const Reader& region : reader.at("regions").items()) {
                extrude.regions.push_back(region.string());
            }
        }
        return extrude;
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
        result["plane"] = sketch->plane;
        if (!sketch->face.empty()) {
            result["face"] = referenceToJson(sketch->face.front());
        }
        // The construction flag is written only when set, to keep documents short.
        const auto curve = [](Json json, bool construction) {
            if (construction) {
                json["construction"] = true;
            }
            return json;
        };
        Json points = Json::array();
        for (const SketchPoint& point : sketch->points) {
            points.push_back(
                {{"id", point.id}, {"x", valueToJson(point.x)}, {"y", valueToJson(point.y)}});
        }
        Json lines = Json::array();
        for (const SketchLine& line : sketch->lines) {
            lines.push_back(curve({{"id", line.id}, {"start", line.start}, {"end", line.end}},
                                  line.construction));
        }
        result["points"] = std::move(points);
        result["lines"] = std::move(lines);
        if (!sketch->arcs.empty()) {
            Json arcs = Json::array();
            for (const SketchArc& arc : sketch->arcs) {
                arcs.push_back(curve({{"id", arc.id},
                                      {"center", arc.center},
                                      {"start", arc.start},
                                      {"end", arc.end}},
                                     arc.construction));
            }
            result["arcs"] = std::move(arcs);
        }
        if (!sketch->circles.empty()) {
            Json circles = Json::array();
            for (const SketchCircle& circle : sketch->circles) {
                circles.push_back(curve({{"id", circle.id},
                                         {"center", circle.center},
                                         {"radius", valueToJson(circle.radius)}},
                                        circle.construction));
            }
            result["circles"] = std::move(circles);
        }
        if (!sketch->constraints.empty()) {
            Json constraints = Json::array();
            for (const SketchConstraint& constraint : sketch->constraints) {
                constraints.push_back({{"id", constraint.id},
                                       {"type", constraint.type},
                                       {"entities", constraint.entities}});
            }
            result["constraints"] = std::move(constraints);
        }
    } else if (const auto* extrude = std::get_if<Extrude>(&feature.data)) {
        result["profile"] = extrude->profile;
        result["distance"] = valueToJson(extrude->distance);
        if (!extrude->regions.empty()) {
            result["regions"] = extrude->regions;
        }
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
    Json component = {
        {"id", document.root.id}, {"name", document.root.name}, {"timeline", std::move(timeline)}};
    // Optional settings are only written when set, so documents without them stay unchanged.
    if (!document.root.bodies.empty()) {
        Json bodies = Json::array();
        for (const BodySettings& body : document.root.bodies) {
            Json entry = {{"id", body.id}, {"name", body.name}, {"visible", body.visible}};
            // Optional, so that older documents and apps read the same settings.
            if (body.opacity != 1.0) {
                entry["opacity"] = body.opacity;
            }
            bodies.push_back(std::move(entry));
        }
        component["bodies"] = std::move(bodies);
    }
    if (!document.root.visibleOrigin.empty()) {
        component["visibleOrigin"] = document.root.visibleOrigin;
    }
    const Json result = {
        {"format", json::kFormat},
        {"formatVersion", kDocumentFormatVersion},
        {"nextId", document.nextId},
        {"parameters", std::move(parameters)},
        {"root", std::move(component)},
        {"rollback", document.rollback ? Json(*document.rollback) : Json(nullptr)},
        {"units", unitSymbol(document.units)},
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
    if (component.has("bodies")) {
        for (const Reader& body : component.at("bodies").items()) {
            document.root.bodies.push_back(
                {.id = body.at("id").string(),
                 .name = body.at("name").string(),
                 .visible = body.at("visible").boolean(),
                 .opacity = body.has("opacity") ? body.at("opacity").number() : 1.0});
        }
    }
    if (component.has("visibleOrigin")) {
        for (const Reader& item : component.at("visibleOrigin").items()) {
            document.root.visibleOrigin.push_back(item.string());
        }
    }
    if (root.has("rollback")) {
        document.rollback = static_cast<std::size_t>(root.at("rollback").unsignedNumber());
    }
    // Version 1 has no units: millimeters.
    if (version >= 2) {
        const Reader units = root.at("units");
        const auto unit = unitFromSymbol(units.string());
        if (!unit) {
            json::invalid("document.units: unknown unit \"" + units.string() + "\"");
        }
        document.units = *unit;
    }
    validate(document);
    return document;
}

}  // namespace facetry::core
