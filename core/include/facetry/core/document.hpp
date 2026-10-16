#pragma once

// The document model (docs/ARCHITECTURE.md#document-model): the description a user edits.
// Geometry is derived from it by regeneration (regeneration.hpp) and is never stored in it.

#include "facetry/core/naming.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace facetry::core {

// Version of the JSON written by toJson(). Older versions are migrated when they are read.
inline constexpr int kDocumentFormatVersion = 1;

// Version of the naming scheme (ADR-0006). Each feature records the version it was created with,
// so a later change to the scheme can migrate old documents instead of silently renaming.
inline constexpr int kNamingVersion = 1;

// A named user parameter, for example width = 40. Expressions arrive with M1.4.
struct Parameter {
    std::string name;
    double value = 0.0;

    friend bool operator==(const Parameter&, const Parameter&) = default;
};

// A number in a feature: a literal, or the name of a user parameter.
struct Value {
    double number = 0.0;
    std::string parameter;  // empty: the value is `number`

    Value() = default;
    // Implicit on purpose, so that literals read naturally: `extrude.distance = 10`.
    Value(double literal) : number(literal) {}
    static Value of(std::string parameterName) {
        Value value;
        value.parameter = std::move(parameterName);
        return value;
    }

    friend bool operator==(const Value&, const Value&) = default;
};

// Where a referenced element was found the last time its feature ran: its ID and fingerprint.
struct Shadow {
    std::uint32_t id = 0;
    ElementGeometry geometry;

    friend bool operator==(const Shadow&, const Shadow&) = default;
};

// A reference to a face, edge or vertex of a body (ADR-0006). The name is what counts; the
// shadow is for messages and for the geometric fallback when the name is gone.
struct Reference {
    std::string body;  // ID of the feature that created the body
    ElementKind kind = ElementKind::Edge;
    std::string name;
    std::optional<Shadow> shadow;

    friend bool operator==(const Reference&, const Reference&) = default;
};

struct SketchPoint {
    std::string id;
    Value x;
    Value y;

    friend bool operator==(const SketchPoint&, const SketchPoint&) = default;
};

struct SketchLine {
    std::string id;
    std::string start;  // point IDs
    std::string end;

    friend bool operator==(const SketchLine&, const SketchLine&) = default;
};

// A sketch on the XY plane whose lines form one closed loop, its profile. Sketch planes,
// constraints and profile detection arrive with M1.3 and M1.4.
struct Sketch {
    std::vector<SketchPoint> points;
    std::vector<SketchLine> lines;

    friend bool operator==(const Sketch&, const Sketch&) = default;
};

// Extrudes a sketch's profile into a new body. The body's ID is the extrude's feature ID.
struct Extrude {
    std::string profile;  // ID of the sketch feature
    Value distance;

    friend bool operator==(const Extrude&, const Extrude&) = default;
};

struct Fillet {
    std::vector<Reference> edges;
    Value radius;

    friend bool operator==(const Fillet&, const Fillet&) = default;
};

using FeatureData = std::variant<Sketch, Extrude, Fillet>;

// One step in the timeline. Executing it turns the model before it into the model after it
// (regeneration.hpp).
struct Feature {
    std::string id;  // unique in the document and never reused, see Document::newId()
    std::string name;
    bool suppressed = false;
    int namingVersion = kNamingVersion;
    FeatureData data;

    // The references the feature depends on, in a fixed order; nullptr for features without
    // references (sketches, extrudes).
    [[nodiscard]] std::vector<Reference>* references() noexcept;
    [[nodiscard]] const std::vector<Reference>* references() const noexcept;

    friend bool operator==(const Feature&, const Feature&) = default;
};

// A component owns a timeline; the bodies and sketches it contains are results of that timeline.
// Child components arrive with M2.6.
struct Component {
    std::string id = "root";
    std::string name = "Root";
    std::vector<Feature> timeline;

    friend bool operator==(const Component&, const Component&) = default;
};

struct Document {
    std::vector<Parameter> parameters;
    Component root;
    // Number of timeline features that regenerate; the ones after the rollback marker are
    // skipped. nullopt means all of them.
    std::optional<std::size_t> rollback;
    // Counter behind newId().
    std::uint64_t nextId = 1;

    // A fresh ID such as "f12" or "l13". IDs are never reused, so a reference to a deleted
    // feature or sketch line cannot silently attach to a new one.
    [[nodiscard]] std::string newId(std::string_view prefix);

    [[nodiscard]] Feature* findFeature(std::string_view id) noexcept;
    [[nodiscard]] const Feature* findFeature(std::string_view id) const noexcept;
    [[nodiscard]] const Parameter* findParameter(std::string_view name) const noexcept;

    friend bool operator==(const Document&, const Document&) = default;
};

// Throws Error(InvalidArgument) if the document breaks a structural rule: duplicate IDs or
// parameter names, sketch lines with unknown points, a rollback marker past the timeline.
// Problems that depend on geometry (a broken reference, a failing fillet) are not errors here;
// regeneration reports them on the feature.
void validate(const Document& document);

// The document as JSON (the future document.json of the native file format, ADR-0008).
[[nodiscard]] std::string toJson(const Document& document);

// Reads a document written by toJson(). Throws Error(InvalidArgument) for malformed input and
// Error(Unsupported) for documents from a newer version.
[[nodiscard]] Document parseDocument(std::string_view json);

}  // namespace facetry::core
