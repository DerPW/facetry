#include "facetry/render/selection.hpp"

#include <algorithm>
#include <cmath>

namespace facetry::render {

void Selection::replace(Element element) {
    elements_.clear();
    if (element.kind != PickKind::None) {
        elements_.push_back(element);
    }
    ++version_;
}

void Selection::toggle(Element element) {
    if (element.kind == PickKind::None) {
        return;
    }
    const auto it = std::find(elements_.begin(), elements_.end(), element);
    if (it != elements_.end()) {
        elements_.erase(it);
    } else {
        elements_.push_back(element);
    }
    ++version_;
}

void Selection::add(Element element) {
    if (element.kind == PickKind::None ||
        std::find(elements_.begin(), elements_.end(), element) != elements_.end()) {
        return;
    }
    elements_.push_back(element);
    ++version_;
}

void Selection::clear() {
    replace({});
}

bool Selection::contains(Element element) const {
    if (std::find(elements_.begin(), elements_.end(), element) != elements_.end()) {
        return true;
    }
    // The shown model is body 1 until there are several (M1.5).
    return element.kind == PickKind::Face &&
           std::find(elements_.begin(), elements_.end(), Element{PickKind::Body, 1}) !=
               elements_.end();
}

namespace {

// Clicks this close count as the same spot.
constexpr float kSameSpot = 4.0F;

}  // namespace

std::size_t ClickCycle::choose(float x, float y, std::size_t count, std::uint64_t version) const {
    if (count == 0 || !last_ || last_->version != version || std::abs(last_->x - x) > kSameSpot ||
        std::abs(last_->y - y) > kSameSpot) {
        return 0;
    }
    return (last_->index + 1) % count;
}

void ClickCycle::chose(float x, float y, std::size_t index, std::uint64_t version) {
    last_ = Click{x, y, index, version};
}

}  // namespace facetry::render
