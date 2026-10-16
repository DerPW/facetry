#pragma once

// An open document: its description, undo/redo, and the regeneration engine with its cache.
// The shells will drive a session through the C API (M1.1).

#include "facetry/core/document.hpp"
#include "facetry/core/regeneration.hpp"

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace facetry::core {

class Session {
  public:
    explicit Session(Document document = {});

    [[nodiscard]] const Document& document() const noexcept {
        return document_;
    }

    // Changes the document as one undo step. If the change throws or leaves the document
    // invalid (see validate()), the document is left as it was and the error is passed on.
    void edit(const std::function<void(Document&)>& change);

    [[nodiscard]] bool canUndo() const noexcept {
        return !undo_.empty();
    }
    [[nodiscard]] bool canRedo() const noexcept {
        return !redo_.empty();
    }
    // Return false if there is nothing to undo or redo.
    bool undo();
    bool redo();

    // Regenerates the document. Afterwards each resolved reference's shadow is updated to where
    // its element is now; that is derived data and not an undo step.
    const Regeneration& regenerate(const CancelToken& cancel = {},
                                   const ProgressCallback& progress = {});

    // The result of the last regenerate() call.
    [[nodiscard]] const Regeneration& regeneration() const noexcept {
        return regeneration_;
    }

    // True if the document changed (edit, undo, redo) since the last complete regeneration.
    [[nodiscard]] bool needsRegeneration() const noexcept {
        return stale_;
    }

    [[nodiscard]] std::string save() const {
        return toJson(document_);
    }
    [[nodiscard]] static Session load(std::string_view json) {
        return Session(parseDocument(json));
    }

  private:
    Document document_;
    std::vector<Document> undo_;
    std::vector<Document> redo_;
    Regenerator regenerator_;
    Regeneration regeneration_;
    bool stale_ = true;
};

}  // namespace facetry::core
