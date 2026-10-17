#pragma once

// The regeneration engine: runs a document's timeline and produces the model, feature by feature.
//
// Feature contract: a feature reads the model before it (bodies and sketch profiles), its
// parameters and its references, and either produces the model after it (shapes with naming,
// see naming.hpp) or fails with an error. A failed feature leaves the model unchanged, and the
// features after it still run.

#include "facetry/core/cancellation.hpp"
#include "facetry/core/document.hpp"
#include "facetry/core/naming.hpp"

#include <atomic>
#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace facetry::core {

// The model at one point of the timeline. Results are shared between cache entries, not copied.
struct ModelState {
    std::map<std::string, std::shared_ptr<const NamedShape>> bodies;  // by creating feature ID
    std::map<std::string, std::shared_ptr<const std::vector<ProfileSegment>>>
        profiles;  // by sketch ID
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
