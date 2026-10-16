# ADR-0011: Document API: sessions with JSON edits over the C API

- **Status:** Accepted
- **Date:** 2026-10-17
- **Milestone:** M1.1 ([#10](https://github.com/DerPW/facetry/issues/10))

## Context

The shells need the document model from M0.6: open a document, edit it, undo and redo,
regenerate, show bodies and feature status, and turn picks into references.
[ADR-0003](0003-core-api-boundary-c-abi.md) set the boundary: a C ABI with handles, status codes
and scalar arguments, wrapped by Swift and TypeScript.

Document data is not scalar. Features have type-specific fields, sketches have lists of points
and lines, references carry names and fingerprints, and a regeneration reports messages per
feature. All of this grows with every feature added in Phase 1 and later. The document already
has a JSON form for saving (`core/include/facetry/core/document.hpp`).

## Options

### One C function per edit and per query

For example `facetry_session_set_parameter(…)` or `facetry_feature_get_type(…)`.

- Every value is typed at the boundary.
- It takes dozens of functions, each mirrored in Swift and TypeScript.
- Lists (sketch points, messages, references) need their own buffer protocols.
- Every new feature type means new C functions and wrapper code.

### JSON for structured data, handles and buffers for geometry

Edits go in as JSON; summaries, names and references come out as JSON. Shapes and meshes keep
their handles and typed buffers.

- One C function per kind of interaction.
- New features need no new C functions.
- Features in edits use the same JSON as the saved document.
- Shells decode the JSON with `Codable` and `JSON.parse`.
- **Costs:**
  - Parsing on every call, negligible for documents of a few kilobytes.
  - The boundary is only typed by documentation. The core validates every edit and reports
    clear messages.

### Everything as messages, including geometry

This is ADR-0003's option C. Meshes would be serialized, so it was rejected again.

## Decision

**JSON for document data, handles and buffers for geometry.** A **session** handle is an open
document. Its functions are listed in `core/include/facetry_core.h` under "Documents":

| Function | Purpose |
|---|---|
| `facetry_session_create`, `_save`, `_release` | Open from JSON, save as JSON, close |
| `facetry_session_apply` | One edit or an array of edits, as one undo step |
| `facetry_session_undo`, `_redo` | Undo history |
| `facetry_session_regenerate` | Regenerate with an optional progress callback |
| `facetry_session_state` | Summary: parameters, features with status and messages, bodies, undo state |
| `facetry_session_body_shape` | A shape handle for a body. Meshes come from the existing tessellation API |
| `facetry_session_element_names` | Persistent names for the element IDs in that body's meshes |
| `facetry_session_make_reference` | A picked element as a reference, ready for a feature edit |

- **Edit and summary formats:** documented in `core/include/facetry/core/session_json.hpp`.
- **Edits are atomic.** An array of edits is one undo step, and a failing edit changes nothing.
- **New features:** without an index, they are inserted at the rollback marker, as in Fusion.
  They get a fresh ID and a default name.
- **Returned strings** are owned by the context and valid until its next call, like the last
  error message.
- **Cancelling:** the progress callback cancels by returning non-zero. The context is locked
  while it regenerates, so a separate cancel function called from another thread would just
  wait for the lock.
- **API version:** this is an addition, so the version goes from 0.1 to 0.2.

## Consequences

- **Shells:**
  - Swift has `SessionState`, `ElementNames` and the `DocumentEdit` enum.
  - TypeScript has the `Edit` union and a `regenerateModel` helper that returns the summary and
    all body meshes in one worker round trip.
- **Feature edits on Apple:** Swift sends `addFeature`/`replaceFeature` edits as JSON until it
  has its own feature types.
- **Generated wrappers:** if the JSON formats settle, Swift and TypeScript types can be
  generated from a schema instead of written by hand.
- **Cancelling on the web:** the worker runs a regeneration to the end before it reads the next
  message, so the UI cannot cancel it yet. That needs a shared flag (SharedArrayBuffer with
  cross-origin isolation) or regeneration in steps. It is tracked in
  [#13](https://github.com/DerPW/facetry/issues/13).
- **Web module size:** the document model is now linked in. It grew by 0.29 MB raw
  (0.1 MB gzip).
