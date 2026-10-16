# Future ideas & non-goals

Ideas that are interesting but have no milestone yet. Anything that does not fit into a
milestone goes here first, so it does not creep into the current phase. Moving an idea into a
phase happens through a pull request that updates the roadmap.

Back to the [roadmap overview](../ROADMAP.md).

## Ideas without a schedule

**Modeling**
- Configurations / variants of one design (Fusion's configured designs)
- Sculpting with T-splines or subdivision surfaces
- Emboss and wrap on curved faces
- Variable-radius and setback fillets
- Wall thickness analysis for 3D printing

**Workflow & collaboration**
- Version history with visual diffs between versions
- Local-first sync and sharing (e.g. iCloud sharing, CRDT-based documents)
- Read-only web viewer: open a `.facetry` file from a link without installing anything
- OpenSCAD import (`.scad` → mesh body via OpenSCAD's WebAssembly build)
- KiCad / ECAD board import for enclosure design

**Output**
- Direct handoff to slicers (Bambu Studio, PrusaSlicer, OrcaSlicer) via URL schemes or local
  integration
- Photorealistic rendering (path tracing; Metal ray tracing on Apple)
- Basic FEA stress simulation with an open-source solver
- CAM toolpaths for CNC milling and laser cutting

**Platforms**
- visionOS app for spatial design review
- Windows, Linux and Android tablets via the web build (works already as soon as the web app
  runs; no native apps planned)

## Non-goals

These are deliberately out of scope, at least until 1.0:

- **Cloud accounts or a server backend.** Facetry works fully offline and stores files where
  you choose.
- **Telemetry, analytics or tracking.**
- **Feature parity with Fusion 360.** The goal is Fusion's core way of working, done well on
  iPad, not all of Fusion.
- **Mesh-first modeling.** Facetry is a solid (B-Rep) modeler; meshes are for import and
  reference.
- **Our own geometry kernel.** We build on OpenCASCADE and contribute fixes upstream where we
  can.
