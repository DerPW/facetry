# Example documents

Documents in the core's JSON format (`facetry-document`, see
[`core/include/facetry/core/document.hpp`](../../core/include/facetry/core/document.hpp)),
checked in so that format changes stay backward compatible. `core/tests/document_test.cpp` and
`core/tests/file_format_test.cpp` load each one, check that it saves back unchanged (older
versions apart from their version number), and regenerate it.

| File | Format version | Content |
|---|---|---|
| `corner-fillet.json` | document 1 | The M0.6 prototype: a rectangle sketch on parameters, extruded, with one vertical edge filleted ([ADR-0006](../../docs/adr/0006-persistent-naming.md)) |
| `corner-fillet.facetry` | file 1, document 1 | The same document as a native file ([ADR-0008](../../docs/adr/0008-native-file-format.md)), written by `facetry-cli pack`; the Apple app uses it as the content of new documents |
| `corner-fillet-inch.json` | document 2 | The same model in inches (1 × 0.5 × 0.25 in, rounded by 0.125 in): document units ([#18](https://github.com/DerPW/facetry/issues/18)) |
| `corner-fillet-inch.facetry` | file 1, document 2 | The inch document as a native file, for opening in the apps |
| `sketch-planes.json` | document 3 | Sketches on planes ([ADR-0013](../../docs/adr/0013-sketch-model.md)): a slot of lines and arcs on XZ with a construction line and constraints, a boss sketched on the slot's end face, and a pin circle on YZ next to a construction circle |
| `sketch-planes.facetry` | file 1, document 3 | The sketch document as a native file |
| `plate-with-hole.json` | document 3 | A rectangle with a circle inside; the extrusion takes the region around the circle, so the plate has a hole ([ADR-0014](../../docs/adr/0014-sketch-profiles.md)) |
| `plate-with-hole.facetry` | file 1, document 3 | The plate as a native file |
| `projected-boss.json` | document 4 | The corner-fillet block without the fillet; a sketch on its top face projects the face's edges ([ADR-0018](../../docs/adr/0018-sketch-projection.md)) and holds a rectangle 1 inside them, extruded 2 into a boss |
| `projected-boss.facetry` | file 1, document 4 | The projection document as a native file |

Never edit a file here to make a test pass. When the format changes, add a migration and keep
the old file; add a new example for the new version.
