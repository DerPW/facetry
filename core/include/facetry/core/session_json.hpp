#pragma once

// The JSON side of the document API (docs/adr/0011-document-api-json-over-c.md): edits that
// shells send to a session, and the summaries they read back. Geometry does not go through JSON;
// shells get shapes and meshes through the C API.

#include "facetry/core/document.hpp"
#include "facetry/core/naming.hpp"
#include "facetry/core/session.hpp"

#include <string>
#include <string_view>

namespace facetry::core {

// Applies one edit, or a JSON array of edits, to `document`. Edits:
//
//   {"op": "setParameter", "name": "width", "value": 12}
//   {"op": "addParameter", "name": "wall", "value": 2}
//   {"op": "deleteParameter", "name": "wall"}
//   {"op": "addFeature", "feature": {…}, "index": 2}
//   {"op": "replaceFeature", "feature": {…}}
//   {"op": "deleteFeature", "id": "f5"}
//   {"op": "renameFeature", "id": "f5", "name": "Corner round"}
//   {"op": "suppressFeature", "id": "f5", "suppressed": true}
//   {"op": "setRollback", "index": 2}        (null: no rollback marker)
//
// Features use the document's JSON form. In "addFeature", "id", "name", "suppressed" and
// "namingVersion" may be left out: the feature gets a fresh ID and a default name. Without an
// "index", a new feature goes to the rollback marker, like in Fusion, or to the end of the
// timeline. "replaceFeature" finds the feature by its "id" and keeps the fields it leaves out.
//
// Throws Error(InvalidArgument) for a malformed or impossible edit. The document may be partly
// changed then; Session::edit() applies edits to a copy, so a session is never left half-edited.
void applyEdits(Document& document, std::string_view json);

// Summary of a session for the shells:
//
//   {"parameters": [{"name": "width", "value": 10}],
//    "rollback": null, "canUndo": true, "canRedo": false, "needsRegeneration": false,
//    "features": [{"id": "f5", "name": "Fillet 1", "type": "fillet", "suppressed": false,
//                  "status": "ok", "messages": [], "brokenReferences": [],
//                  "dependents": [], "definition": {…}}],
//    "bodies": ["f2"],
//    "regeneration": {"cancelled": false, "executed": 1}}
//
// "status" is "ok", "warning", "error", "suppressed", "rolledBack" or "notRun"; it comes from the
// last regeneration and is "notRun" for features added since. "dependents" lists the later
// features that use this one, directly or through others (what deleting it would break).
// "definition" is the feature in the document's JSON form, ready to be changed and sent back with
// "replaceFeature".
[[nodiscard]] std::string sessionStateJson(const Session& session);

// Persistent names of a body's elements: {"faces": [...], "edges": [...], "vertices": [...]}.
// Entry i belongs to element ID i + 1, as in meshes of the body's shape.
[[nodiscard]] std::string elementNamesJson(const NamedShape& shape);

// A reference as JSON, ready to be used in a feature (e.g. a fillet's "edges").
[[nodiscard]] std::string referenceJson(const Reference& reference);

}  // namespace facetry::core
