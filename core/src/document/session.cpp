#include "facetry/core/session.hpp"

#include <utility>

namespace facetry::core {

Session::Session(Document document) : document_(std::move(document)) {
    validate(document_);
}

void Session::edit(const std::function<void(Document&)>& change) {
    Document next = document_;
    change(next);
    validate(next);
    undo_.push_back(std::move(document_));
    document_ = std::move(next);
    redo_.clear();
}

bool Session::undo() {
    if (undo_.empty()) {
        return false;
    }
    redo_.push_back(std::move(document_));
    document_ = std::move(undo_.back());
    undo_.pop_back();
    return true;
}

bool Session::redo() {
    if (redo_.empty()) {
        return false;
    }
    undo_.push_back(std::move(document_));
    document_ = std::move(redo_.back());
    redo_.pop_back();
    return true;
}

const Regeneration& Session::regenerate(const CancelToken& cancel,
                                        const ProgressCallback& progress) {
    regeneration_ = regenerator_.run(document_, cancel, progress);
    std::vector<Feature>& timeline = document_.root.timeline;
    for (std::size_t i = 0; i < regeneration_.features.size() && i < timeline.size(); ++i) {
        const FeatureResult& result = regeneration_.features[i];
        std::vector<Reference>* references = timeline[i].references();
        if (references == nullptr ||
            (result.status != FeatureStatus::Ok && result.status != FeatureStatus::Warning)) {
            continue;
        }
        for (std::size_t j = 0; j < references->size() && j < result.resolved.size(); ++j) {
            if (result.resolved[j]) {
                (*references)[j].shadow = result.resolved[j];
            }
        }
    }
    return regeneration_;
}

}  // namespace facetry::core
