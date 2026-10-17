#include "facetry/core/session.hpp"

#include "facetry/core/error.hpp"

#include <utility>

namespace facetry::core {

Session::Session(Document document) : document_(std::move(document)) {
    validate(document_);
}

void Session::edit(const std::function<void(Document&)>& change, std::string_view name) {
    Document next = document_;
    change(next);
    validate(next);
    if (next == document_) {
        return;
    }
    if (!transaction_) {
        undo_.push_back({.document = std::move(document_), .name = std::string(name)});
        redo_.clear();
    }
    document_ = std::move(next);
    stale_ = true;
}

void Session::beginTransaction(std::string_view name) {
    if (transaction_) {
        throw Error(ErrorCode::InvalidArgument,
                    "\"" + transaction_->name + "\" is still open; finish it first");
    }
    transaction_ = Step{.document = document_, .name = std::string(name)};
}

void Session::commitTransaction() {
    if (!transaction_) {
        throw Error(ErrorCode::InvalidArgument, "There is no open transaction to commit");
    }
    Step step = std::move(*transaction_);
    transaction_.reset();
    if (step.document != document_) {
        undo_.push_back(std::move(step));
        redo_.clear();
    }
}

void Session::cancelTransaction() {
    if (!transaction_) {
        throw Error(ErrorCode::InvalidArgument, "There is no open transaction to cancel");
    }
    if (transaction_->document != document_) {
        document_ = std::move(transaction_->document);
        stale_ = true;
    }
    transaction_.reset();
}

std::string_view Session::undoName() const noexcept {
    return undo_.empty() ? std::string_view{} : std::string_view(undo_.back().name);
}

std::string_view Session::redoName() const noexcept {
    return redo_.empty() ? std::string_view{} : std::string_view(redo_.back().name);
}

namespace {

void requireNoTransaction(bool open) {
    if (open) {
        throw Error(ErrorCode::InvalidArgument, "Finish the current tool before undo or redo");
    }
}

}  // namespace

bool Session::undo() {
    requireNoTransaction(transaction_.has_value());
    if (undo_.empty()) {
        return false;
    }
    Step step = std::move(undo_.back());
    undo_.pop_back();
    redo_.push_back({.document = std::move(document_), .name = step.name});
    document_ = std::move(step.document);
    stale_ = true;
    return true;
}

bool Session::redo() {
    requireNoTransaction(transaction_.has_value());
    if (redo_.empty()) {
        return false;
    }
    Step step = std::move(redo_.back());
    redo_.pop_back();
    undo_.push_back({.document = std::move(document_), .name = step.name});
    document_ = std::move(step.document);
    stale_ = true;
    return true;
}

const Regeneration& Session::regenerate(const CancelToken& cancel,
                                        const ProgressCallback& progress) {
    regeneration_ = regenerator_.run(document_, cancel, progress);
    stale_ = regeneration_.cancelled;
    std::vector<Feature>& timeline = document_.root.timeline;
    for (std::size_t i = 0; i < regeneration_.features.size() && i < timeline.size(); ++i) {
        const FeatureResult& result = regeneration_.features[i];
        if (result.status != FeatureStatus::Ok && result.status != FeatureStatus::Warning) {
            continue;
        }
        const std::vector<Reference*> references = timeline[i].references();
        for (std::size_t j = 0; j < references.size() && j < result.resolved.size(); ++j) {
            if (result.resolved[j]) {
                references[j]->shadow = result.resolved[j];
            }
        }
        // Projected entities are stored where their sources put them (#52), so tools and a lost
        // source see them there.
        auto* sketch = std::get_if<Sketch>(&timeline[i].data);
        const auto geometry = regeneration_.model.sketches.find(timeline[i].id);
        if (sketch == nullptr || geometry == regeneration_.model.sketches.end()) {
            continue;
        }
        const double scale = millimetersPer(document_.units);
        for (SketchPoint& point : sketch->points) {
            const auto at = geometry->second->projectedPoints.find(point.id);
            if (at != geometry->second->projectedPoints.end()) {
                point.x = Value(at->second.first / scale);
                point.y = Value(at->second.second / scale);
            }
        }
        for (SketchCircle& circle : sketch->circles) {
            const auto radius = geometry->second->projectedRadii.find(circle.id);
            if (radius != geometry->second->projectedRadii.end()) {
                circle.radius = Value(radius->second / scale);
            }
        }
    }
    return regeneration_;
}

}  // namespace facetry::core
