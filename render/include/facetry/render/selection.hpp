#pragma once

// The selection: any number of vertices, edges, faces and bodies (#36). The renderer
// highlights it; the shells add to it by clicking and turn it into references.

#include "facetry/render/pick.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace facetry::render {

class Selection {
  public:
    // Selects only `element`; PickKind::None clears the selection.
    void replace(Element element);
    // Adds `element`, or removes it if it is selected already. PickKind::None changes nothing.
    void toggle(Element element);
    // Adds `element` unless it is selected already.
    void add(Element element);
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

// Clicking the same spot again picks the next of the candidates there (#38): the edge behind a
// vertex, the face behind a face. Remembers the last click.
class ClickCycle {
  public:
    // Which of `count` candidates a click at (x, y) means: the next one after a click at the
    // same spot that left the selection as it was (`version`), the first one otherwise.
    [[nodiscard]] std::size_t choose(float x, float y, std::size_t count,
                                     std::uint64_t version) const;
    // Remembers the click and the selection's version after it.
    void chose(float x, float y, std::size_t index, std::uint64_t version);
    void reset() {
        last_.reset();
    }

  private:
    struct Click {
        float x;
        float y;
        std::size_t index;
        std::uint64_t version;
    };
    std::optional<Click> last_;
};

}  // namespace facetry::render
