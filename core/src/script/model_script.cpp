#include "facetry/script/model_script.hpp"

#include "facetry/core/error.hpp"
#include "facetry/core/modeling.hpp"

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdint>
#include <map>
#include <sstream>

namespace facetry::script {
namespace {

using core::Error;
using core::ErrorCode;
using nlohmann::json;

json parse(std::string_view text) {
    try {
        return json::parse(text);
    } catch (const json::exception& error) {
        throw Error(ErrorCode::InvalidArgument, std::string("Invalid model JSON: ") + error.what());
    }
}

[[noreturn]] void invalid(const std::string& message) {
    throw Error(ErrorCode::InvalidArgument, message);
}

const json& field(const json& object, const char* name, const std::string& context) {
    if (!object.is_object() || !object.contains(name)) {
        invalid(context + ": missing \"" + name + "\"");
    }
    return object.at(name);
}

double number(const json& object, const char* name, const std::string& context) {
    const json& value = field(object, name, context);
    if (!value.is_number()) {
        invalid(context + ": \"" + name + "\" must be a number");
    }
    return value.get<double>();
}

std::vector<double> numbers(const json& object, const char* name, std::size_t count,
                            const std::string& context) {
    const json& value = field(object, name, context);
    if (!value.is_array() || value.size() != count) {
        invalid(context + ": \"" + name + "\" must be an array of " + std::to_string(count) +
                " numbers");
    }
    std::vector<double> result;
    for (const json& item : value) {
        if (!item.is_number()) {
            invalid(context + ": \"" + name + "\" must contain numbers only");
        }
        result.push_back(item.get<double>());
    }
    return result;
}

std::string text(const json& object, const char* name, const std::string& context) {
    const json& value = field(object, name, context);
    if (!value.is_string()) {
        invalid(context + ": \"" + name + "\" must be a string");
    }
    return value.get<std::string>();
}

// Edges are given as a list of IDs or as a selector object, e.g. {"parallelTo": [0, 0, 1]}.
std::vector<std::uint32_t> selectEdges(const core::Shape& shape, const json& selection,
                                       const std::string& context) {
    if (selection.is_array()) {
        std::vector<std::uint32_t> ids;
        for (const json& id : selection) {
            if (!id.is_number_unsigned()) {
                invalid(context + ": edge IDs must be positive integers");
            }
            ids.push_back(id.get<std::uint32_t>());
        }
        return ids;
    }
    if (selection.is_object() && selection.contains("parallelTo")) {
        const std::vector<double> d = numbers(selection, "parallelTo", 3, context);
        auto ids = core::straightEdgesParallelTo(shape, {d[0], d[1], d[2]});
        if (ids.empty()) {
            invalid(context + ": no straight edge is parallel to the given direction");
        }
        return ids;
    }
    invalid(context + ": \"edges\" must be a list of IDs or {\"parallelTo\": [x, y, z]}");
}

}  // namespace

core::Shape buildModel(std::string_view jsonText) {
    const json model = parse(jsonText);
    const json& operations = field(model, "operations", "model");
    if (!operations.is_array() || operations.empty()) {
        invalid("model: \"operations\" must be a non-empty array");
    }

    std::map<std::string, core::Shape> shapes;
    const auto shapeRef = [&](const json& op, const char* name, const std::string& context) {
        const std::string id = text(op, name, context);
        const auto it = shapes.find(id);
        if (it == shapes.end()) {
            invalid(context + ": unknown shape \"" + id + "\"");
        }
        return it->second;
    };

    for (std::size_t i = 0; i < operations.size(); ++i) {
        const json& op = operations[i];
        const std::string context = "operation " + std::to_string(i + 1);
        const std::string id = text(op, "id", context);
        const std::string kind = text(op, "op", context);
        if (shapes.contains(id)) {
            invalid(context + ": duplicate id \"" + id + "\"");
        }

        core::Shape result;
        if (kind == "box") {
            const auto size = numbers(op, "size", 3, context);
            result = core::makeBox(size[0], size[1], size[2]);
        } else if (kind == "cylinder") {
            const auto center = numbers(op, "center", 2, context);
            result = core::makeCylinder(center[0], center[1], number(op, "radius", context),
                                        number(op, "height", context));
        } else if (kind == "fuse" || kind == "cut" || kind == "common") {
            const core::BooleanOp booleanOp = kind == "fuse"  ? core::BooleanOp::Fuse
                                              : kind == "cut" ? core::BooleanOp::Cut
                                                              : core::BooleanOp::Common;
            result = core::booleanOp(booleanOp, shapeRef(op, "shape", context),
                                     shapeRef(op, "tool", context));
        } else if (kind == "fillet") {
            const core::Shape source = shapeRef(op, "shape", context);
            const auto edges = selectEdges(source, field(op, "edges", context), context);
            result = core::fillet(source, edges, number(op, "radius", context));
        } else {
            invalid(context + ": unknown operation \"" + kind + "\"");
        }
        shapes.emplace(id, std::move(result));
    }

    const std::string resultId = text(model, "result", "model");
    const auto it = shapes.find(resultId);
    if (it == shapes.end()) {
        invalid("model: result \"" + resultId + "\" is not an operation id");
    }
    return it->second;
}

std::vector<std::string> checkExpected(const core::Shape& shape, std::string_view jsonText) {
    const json model = parse(jsonText);
    const json& expected = field(model, "expected", "model");
    const double tolerance = expected.value("tolerance", 1e-6);
    const core::ShapeProperties actual = shape.properties();
    std::vector<std::string> mismatches;

    const auto compare = [&](const char* name, double value, double want) {
        const double allowed = tolerance * std::max(1.0, std::abs(want));
        if (!(std::abs(value - want) <= allowed)) {
            std::ostringstream message;
            message.precision(10);
            message << name << ": expected " << want << ", got " << value;
            mismatches.push_back(message.str());
        }
    };
    const auto compareCount = [&](const char* name, std::size_t value) {
        if (expected.contains(name)) {
            const auto want = expected.at(name).get<std::size_t>();
            if (value != want) {
                mismatches.push_back(std::string(name) + ": expected " + std::to_string(want) +
                                     ", got " + std::to_string(value));
            }
        }
    };

    if (expected.contains("volume")) {
        compare("volume", actual.volume, expected.at("volume").get<double>());
    }
    if (expected.contains("area")) {
        compare("area", actual.area, expected.at("area").get<double>());
    }
    if (expected.contains("bbox")) {
        const json& box = expected.at("bbox");
        const double values[6] = {actual.bounds.min.x, actual.bounds.min.y, actual.bounds.min.z,
                                  actual.bounds.max.x, actual.bounds.max.y, actual.bounds.max.z};
        static constexpr const char* names[6] = {"bbox min x", "bbox min y", "bbox min z",
                                                 "bbox max x", "bbox max y", "bbox max z"};
        for (std::size_t i = 0; i < 6; ++i) {
            compare(names[i], values[i], box.at(i / 3).at(i % 3).get<double>());
        }
    }
    compareCount("solids", actual.counts.solids);
    compareCount("faces", actual.counts.faces);
    compareCount("edges", actual.counts.edges);
    compareCount("vertices", actual.counts.vertices);
    return mismatches;
}

}  // namespace facetry::script
