# Example documents

Documents in the core's JSON format (`facetry-document`, see
[`core/include/facetry/core/document.hpp`](../../core/include/facetry/core/document.hpp)),
checked in so that format changes stay backward compatible. `core/tests/document_test.cpp` and
`core/tests/file_format_test.cpp` load each one, check that it saves back unchanged, and
regenerate it.

| File | Format version | Content |
|---|---|---|
| `corner-fillet.json` | document 1 | The M0.6 prototype: a rectangle sketch on parameters, extruded, with one vertical edge filleted ([ADR-0006](../../docs/adr/0006-persistent-naming.md)) |
| `corner-fillet.facetry` | file 1, document 1 | The same document as a native file ([ADR-0008](../../docs/adr/0008-native-file-format.md)), written by `facetry-cli pack`; the Apple app uses it as the content of new documents |

Never edit a file here to make a test pass. When the format changes, add a migration and keep
the old file; add a new example for the new version.
