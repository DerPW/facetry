#pragma once

// The selection: any number of vertices, edges, faces and bodies (#36). The renderer
// highlights it; the shells add to it by clicking and turn it into references.

#include "facetry/render/pick.hpp"

#include <cstdint>
#include <vector>

namespace facetry::render {

struct Element {
    PickKind kind = PickKind::None;
    std::uint32_t id = 0;

    friend bool operator==(const Element&, const Element&) = default;
};

class Selection {
  public:
    // Selects only `element`; PickKind::None clears the selection.
    void replace(Element element);
    // Adds `element`, or removes it if it is selected already. PickKind::None changes nothing.
    void toggle(Element element);
    void clear();

    // Whether the element is selected itself or, for a face, through its body.
    [[nodiscard]] bool contains(Element element) const;
    [[nodiscard]] const std::vector<Element>& elements() const {
        return elements_;
    }
    // Increases with every change, so the renderer knows when to update its highlights.
    [[nodiscard]] std::uint64_t version() const {
        return version_;
    }

  private:
    std::vector<Element> elements_;
    std::uint64_t version_ = 0;
};

}  // namespace facetry::render
