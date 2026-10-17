#include "facetry/render/selection.hpp"

#include <algorithm>

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

}  // namespace facetry::render
