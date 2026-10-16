# Example documents

Documents in the core's JSON format (`facetry-document`, see
[`core/include/facetry/core/document.hpp`](../../core/include/facetry/core/document.hpp)),
checked in so that format changes stay backward compatible: `core/tests/document_test.cpp` loads
each one, checks that it saves back unchanged, and regenerates it.

| File | Format version | Content |
|---|---|---|
| `corner-fillet.json` | 1 | The M0.6 prototype: a rectangle sketch on parameters, extruded, with one vertical edge filleted ([ADR-0006](../../docs/adr/0006-persistent-naming.md)) |

Never edit a file here to make a test pass. When the format changes, add a migration and keep
the old file; add a new example for the new version.
