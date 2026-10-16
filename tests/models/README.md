# Benchmark models

Reference models as JSON descriptions with their expected results. The core tests and
`facetry-cli check` regenerate every model and compare it with its `expected` section, so any
change in modeling behavior shows up in CI.

The models are defined in the [roadmap](../../docs/ROADMAP.md#benchmark-models). This simple
operation list is a stand-in until the native document format exists (M1.1).

## Format

```json
{
  "description": "What the model is and why it is here.",
  "operations": [
    { "id": "box", "op": "box", "size": [20, 10, 5] },
    { "id": "rounded", "op": "fillet", "shape": "box", "edges": { "parallelTo": [0, 0, 1] }, "radius": 2 }
  ],
  "result": "rounded",
  "expected": {
    "tolerance": 1e-6,
    "volume": 982.831853072,
    "area": 675.964594301,
    "bbox": [[0, 0, 0], [20, 10, 5]],
    "solids": 1, "faces": 10, "edges": 24, "vertices": 16,
    "notes": "How the expected values were obtained."
  }
}
```

| Operation | Fields |
|---|---|
| `box` | `size: [dx, dy, dz]`, one corner at the origin |
| `cylinder` | `center: [x, y]`, `radius`, `height`, standing on the XY plane |
| `fuse`, `cut`, `common` | `shape`, `tool`: ids of earlier operations |
| `fillet` | `shape`, `radius`, `edges`: a list of edge IDs or `{ "parallelTo": [x, y, z] }` |

`tolerance` is relative (`|actual - expected| <= tolerance * max(1, |expected|)`). Counts must
match exactly. Every field in `expected` is optional.

## Adding a model

1. Write the operations and run `facetry-cli info <model.json>` to see the result.
2. Check the numbers independently where possible (by hand, or in another CAD tool), and say
   how in `notes`.
3. Add the `expected` section and run `facetry-cli check tests/models`.
