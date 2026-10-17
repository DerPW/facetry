#pragma once

// The document model (docs/ARCHITECTURE.md#document-model): the description a user edits.
// Geometry is derived from it by regeneration (regeneration.hpp) and is never stored in it.

#include "facetry/core/naming.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace facetry::core {

// Version of the JSON written by toJson(). Older versions are migrated when they are read:
// version 1 has no units and is in millimeters; up to version 2 sketches lie on XY and hold only
// points and lines; up to version 3 sketches project nothing.
inline constexpr int kDocumentFormatVersion = 4;

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

// Curves refer to their points by ID. Construction curves help drawing but never bound a profile.
struct SketchLine {
    std::string id;
    std::string start;  // point IDs
    std::string end;
    bool construction = false;

    friend bool operator==(const SketchLine&, const SketchLine&) = default;
};

// An arc around `center`, counter-clockwise from `start` to `end` as seen from the side the
// sketch's normal points to. `end` lies on the circle through `start`.
struct SketchArc {
    std::string id;
    std::string center;  // point IDs
    std::string start;
    std::string end;
    bool construction = false;

    friend bool operator==(const SketchArc&, const SketchArc&) = default;
};

struct SketchCircle {
    std::string id;
    std::string center;  // point ID
    Value radius;
    bool construction = false;

    friend bool operator==(const SketchCircle&, const SketchCircle&) = default;
};

// A geometric constraint, kept for the solver (M1.4, ADR-0005): its type and the IDs of the
// points and curves it binds. Types: see kSketchConstraintTypes.
struct SketchConstraint {
    std::string id;
    std::string type;
    std::vector<std::string> entities;

    friend bool operator==(const SketchConstraint&, const SketchConstraint&) = default;
};

// Constraint types and how many entities each binds (at least, at most).
struct SketchConstraintType {
    std::string_view name;
    std::size_t minEntities;
    std::size_t maxEntities;
};
inline constexpr SketchConstraintType kSketchConstraintTypes[] = {
    {"coincident", 2, 2},     // two points, or a point on a curve
    {"horizontal", 1, 2},     // a line, or two points
    {"vertical", 1, 2},       // a line, or two points
    {"parallel", 2, 2},       // two lines
    {"perpendicular", 2, 2},  // two lines
    {"tangent", 2, 2},        // two curves
    {"equal", 2, 2},          // two lines (length) or two arcs or circles (radius)
    {"midpoint", 2, 2},       // a point and a line
    {"concentric", 2, 2},     // two arcs or circles
    {"fix", 1, 1},            // any entity
};

// A projection (#52, ADR-0018): an origin item or a body's edge, projected onto the sketch plane.
// It drives ordinary entities of the sketch: `curve`, a line, arc or circle (none for the origin
// point), and `points`, a line's start and end, an arc's center, start and end, a circle's center,
// or the origin point. Regeneration puts them where the source lies; their stored values are where
// it was last, so curves drawn on them keep their place when the source is lost.
struct SketchProjection {
    std::string id;
    std::string origin;           // an origin item, "o", "x", "y" or "z"; empty for an edge
    std::vector<Reference> edge;  // one edge reference when `origin` is empty
    std::string curve;
    std::vector<std::string> points;

    friend bool operator==(const SketchProjection&, const SketchProjection&) = default;
};

// A sketch (ADR-0013): points and curves with stable IDs (ADR-0006) on a plane. Its profiles are
// the regions its curves enclose.
struct Sketch {
    // An origin plane ("xy", "xz" or "yz"), or "face": the planar face `face` refers to.
    std::string plane = "xy";
    std::vector<Reference> face;  // one reference when `plane` is "face", empty otherwise
    std::vector<SketchPoint> points;
    std::vector<SketchLine> lines;
    std::vector<SketchArc> arcs;
    std::vector<SketchCircle> circles;
    std::vector<SketchConstraint> constraints;
    std::vector<SketchProjection> projections;

    // The projection that drives a point or curve, if one does.
    [[nodiscard]] const SketchProjection* projectionOf(std::string_view entity) const noexcept;

    friend bool operator==(const Sketch&, const Sketch&) = default;
};

// Extrudes a sketch's profile into a new body. The body's ID is the extrude's feature ID.
struct Extrude {
    std::string profile;  // ID of the sketch feature
    Value distance;
    // Names of the sketch's regions to extrude (ADR-0014). Empty takes the sketch's only region.
    std::vector<std::string> regions;

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

    // The references the feature depends on, in a fixed order: a fillet's edges; a sketch's face,
    // if it lies on one, then the edges it projects. Extrudes have none.
    [[nodiscard]] std::vector<Reference*> references();
    [[nodiscard]] std::vector<const Reference*> references() const;

    // Calls `visit` on every value of the feature; all of them are lengths.
    void forEachValue(const std::function<void(Value&)>& visit);

    friend bool operator==(const Feature&, const Feature&) = default;
};

// How a body is shown. A body's ID is the ID of the feature that created it; only bodies the user
// renamed or hid have settings.
struct BodySettings {
    std::string id;
    std::string name;  // empty: the default name ("Body 1", …)
    bool visible = true;
    double opacity = 1.0;  // 1 opaque, down to 0 (invisible but still selectable)

    friend bool operator==(const BodySettings&, const BodySettings&) = default;
};

// One of the fixed elements of a component's origin.
struct OriginItem {
    std::string_view id;    // "xy", "xz", "yz", "x", "y", "z", "o"
    std::string_view name;  // "XY plane", …
    std::string_view kind;  // "plane", "axis" or "point"
};

// The origin every component has: the three base planes, the three axes and the origin point.
[[nodiscard]] std::span<const OriginItem> originItems() noexcept;

// The unit of a document's lengths (#18). Values are stored, shown and entered in it; geometry
// is built in millimeters. Parameters are lengths until they get units of their own (M1.4).
enum class LengthUnit : std::uint8_t { Millimeter, Centimeter, Meter, Inch };

// Millimeters per unit, e.g. 25.4 for inches.
[[nodiscard]] double millimetersPer(LengthUnit unit) noexcept;
// "mm", "cm", "m" or "in".
[[nodiscard]] std::string_view unitSymbol(LengthUnit unit) noexcept;
// The unit with this symbol, if there is one.
[[nodiscard]] std::optional<LengthUnit> unitFromSymbol(std::string_view symbol) noexcept;

// A component owns a timeline; the bodies and sketches it contains are results of that timeline.
// Child components arrive with M2.6.
struct Component {
    std::string id = "root";
    std::string name = "Root";
    std::vector<Feature> timeline;
    std::vector<BodySettings> bodies;        // only bodies with non-default settings
    std::vector<std::string> visibleOrigin;  // origin items shown (all are hidden by default)

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
    LengthUnit units = LengthUnit::Millimeter;

    // A fresh ID such as "f12" or "l13". IDs are never reused, so a reference to a deleted
    // feature or sketch line cannot silently attach to a new one.
    [[nodiscard]] std::string newId(std::string_view prefix);

    [[nodiscard]] Feature* findFeature(std::string_view id) noexcept;
    [[nodiscard]] const Feature* findFeature(std::string_view id) const noexcept;
    [[nodiscard]] const Parameter* findParameter(std::string_view name) const noexcept;

    // The features that create bodies (extrudes), in timeline order.
    [[nodiscard]] std::vector<const Feature*> bodyFeatures() const;
    // A body's name: its setting, or "Body N" in the order the bodies are created.
    [[nodiscard]] std::string bodyName(std::string_view body) const;
    [[nodiscard]] bool bodyVisible(std::string_view body) const;
    [[nodiscard]] double bodyOpacity(std::string_view body) const;

    friend bool operator==(const Document&, const Document&) = default;
};

// Changes the document's unit and converts every length into it, so the geometry stays the same.
void convertUnits(Document& document, LengthUnit units);

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
