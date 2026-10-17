#pragma once

// An open document: its description, undo/redo, and the regeneration engine with its cache.
// The shells drive a session through the C API (docs/adr/0011-document-api-json-over-c.md).
//
// Undo works on commands: every edit (or transaction, see beginTransaction) is one named step,
// kept as the document before it. Documents are small (kilobytes), so a step costs one copy.

#include "facetry/core/document.hpp"
#include "facetry/core/regeneration.hpp"

#include <functional>
#include <optional>
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

    // Changes the document as one undo step named `name` (e.g. "Rename feature"). If the change
    // throws or leaves the document invalid (see validate()), the document is left as it was and
    // the error is passed on. A change that changes nothing adds no step. Inside a transaction
    // the change joins the transaction's step.
    void edit(const std::function<void(Document&)>& change, std::string_view name = "Edit");

    // Groups the edits until commitTransaction() into one undo step, e.g. everything a tool does
    // until the user confirms it. cancelTransaction() restores the document from before.
    // Throws Error(InvalidArgument) if a transaction is already open, or on commit or cancel
    // without one.
    void beginTransaction(std::string_view name);
    void commitTransaction();
    void cancelTransaction();
    [[nodiscard]] bool inTransaction() const noexcept {
        return transaction_.has_value();
    }

    [[nodiscard]] bool canUndo() const noexcept {
        return !undo_.empty() && !transaction_;
    }
    [[nodiscard]] bool canRedo() const noexcept {
        return !redo_.empty() && !transaction_;
    }
    // Number of steps undo() can take back.
    [[nodiscard]] std::size_t undoCount() const noexcept {
        return undo_.size();
    }
    // The names of the steps undo() and redo() would take back or repeat; empty if none.
    [[nodiscard]] std::string_view undoName() const noexcept;
    [[nodiscard]] std::string_view redoName() const noexcept;
    // Return false if there is nothing to undo or redo. Throw Error(InvalidArgument) inside a
    // transaction.
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
    // A step: the document on the other side of it, and the step's name.
    struct Step {
        Document document;
        std::string name;
    };

    Document document_;
    std::vector<Step> undo_;
    std::vector<Step> redo_;
    std::optional<Step> transaction_;  // the document before the open transaction
    Regenerator regenerator_;
    Regeneration regeneration_;
    bool stale_ = true;
};

}  // namespace facetry::core
