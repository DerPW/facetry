#pragma once

// The regeneration engine: runs a document's timeline and produces the model, feature by feature.
//
// Feature contract: a feature reads the model before it (bodies and sketch profiles), its
// parameters and its references, and either produces the model after it (shapes with naming,
// see naming.hpp) or fails with an error. A failed feature leaves the model unchanged, and the
// features after it still run.

#include "facetry/core/cancellation.hpp"
#include "facetry/core/document.hpp"
#include "facetry/core/mesh.hpp"
#include "facetry/core/naming.hpp"

#include <atomic>
#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace facetry::core {

// A curve of a sketch, in sketch coordinates; its segment is named "<sketch>/<curve>".
struct SketchCurve {
    ProfileSegment segment;
    std::string start;  // point IDs; empty for circles
    std::string end;
    bool construction = false;
};

// What a sketch adds to the model: its plane, its curves and points and the regions they enclose.
struct SketchGeometry {
    SketchFrame frame;
    std::vector<SketchCurve> curves;    // its lines, then arcs, then circles, in document order
    std::vector<SketchRegion> regions;  // sorted by name
    std::vector<Vec3> points;           // in space, in document order
    // Where projections (#52) put the points and circle radii they drive, in millimeters on the
    // plane. Sessions store them in the document; broken projections are missing here.
    std::map<std::string, std::pair<double, double>> projectedPoints{};
    std::map<std::string, double> projectedRadii{};
};

// A sketch for display: curve i is edge ID i + 1 (as in SketchGeometry::curves), sampled so that
// arcs and circles stay within `deflection` of the true curve, in steps of at most 2°; point i is
// vertex ID i + 1. There are no triangles.
[[nodiscard]] Mesh sketchMesh(const SketchGeometry& sketch, double deflection);

// The same for any curves and points on a sketch plane (sketch coordinates in millimeters).
[[nodiscard]] Mesh curvesMesh(const SketchFrame& frame, std::span<const ProfileSegment> curves,
                              std::span<const Vec3> points, double deflection);

// Where a sketch's constraints show as glyphs (#50): one per constraint, or one on each of the
// two curves for parallel, perpendicular and equal; in space, in millimeters. Constraints on
// entities that are gone are left out.
struct ConstraintGlyph {
    std::string type;  // the constraint type
    std::string constraint;
    Vec3 at;
};
[[nodiscard]] std::vector<ConstraintGlyph> constraintGlyphs(const Sketch& sketch,
                                                            const SketchGeometry& geometry);

// A length value in millimeters: a literal or a parameter, in the document's units. Throws
// Error(InvalidArgument) for a parameter that does not exist.
[[nodiscard]] double lengthInMillimeters(const Value& value, const Document& document);

// The model at one point of the timeline. Results are shared between cache entries, not copied.
struct ModelState {
    std::map<std::string, std::shared_ptr<const NamedShape>> bodies;  // by creating feature ID
    std::map<std::string, std::shared_ptr<const SketchGeometry>> sketches;  // by sketch ID
};

enum class FeatureStatus {
    Ok,
    Warning,     // ran, but something needs attention (see messages)
    Error,       // did not run; the model passes through unchanged
    Suppressed,  // switched off by the user
    RolledBack,  // after the rollback marker
    NotRun,      // regeneration was cancelled before this feature
};

struct FeatureResult {
    std::string feature;  // feature ID
    FeatureStatus status = FeatureStatus::NotRun;
    std::vector<std::string> messages;
    // Indices into Feature::references() that could not be resolved. A repair UI lets the user
    // pick a replacement for each (M1.1).
    std::vector<std::size_t> brokenReferences;
    // Where each reference resolved, in the order of Feature::references(); nullopt if not.
    std::vector<std::optional<Shadow>> resolved;
    bool fromCache = false;
};

struct Regeneration {
    std::vector<FeatureResult> features;  // one per timeline feature, in timeline order
    ModelState model;  // the model at the rollback marker, or where a cancelled run stopped
    bool cancelled = false;
    std::size_t executed = 0;  // features that ran, as opposed to coming from the cache

    [[nodiscard]] const FeatureResult* find(std::string_view feature) const noexcept;
};

// Turns a selected element (a body and an element ID in it) into a persistent reference.
// Throws Error(InvalidArgument) if the body or element does not exist.
[[nodiscard]] Reference makeReference(const ModelState& model, std::string_view body,
                                      ElementKind kind, std::uint32_t id);

// Reports progress: `done` of `total` features have been handled. Also called from time to time
// during long kernel operations (with the same `done`), so it can cancel the token mid-feature.
using ProgressCallback = std::function<void(std::size_t done, std::size_t total)>;

// Regenerates documents and keeps a cache of each feature's result. A feature runs again only if
// its definition (with parameter values filled in) or anything before it changed.
class Regenerator {
  public:
    // Checks `cancel` between features and inside long kernel operations. After a cancellation
    // the features that finished stay cached, so the next run continues where this one stopped;
    // a feature stopped halfway is not cached.
    Regeneration run(const Document& document, const CancelToken& cancel = {},
                     const ProgressCallback& progress = {});

    void clearCache() noexcept {
        cache_.clear();
    }

  private:
    struct CacheEntry {
        std::string key;
        FeatureResult result;
        ModelState after;
    };
    std::vector<CacheEntry> cache_;
};

}  // namespace facetry::core
