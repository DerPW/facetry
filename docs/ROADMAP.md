# Facetry Roadmap

This is the plan for building Facetry step by step: from a technical foundation to a usable
parametric modeler, then to advanced modeling, assemblies and drawings.

The roadmap is split into **phases** (big goals) and **milestones** (shippable chunks of work).
Each phase has its own document with the detailed feature list:

| Phase | Goal | Milestones | Ends with |
|---|---|---|---|
| [0 — Foundation](roadmap/phase-0-foundation.md) | Prove the whole stack end to end on iPad, Mac and web | M0.1 – M0.7 | v0.0.1 tech preview |
| [1 — Core Modeling](roadmap/phase-1-core-modeling.md) | Model real, simple parts: sketch → extrude → fillet → export | M1.1 – M1.10 | **v0.1 MVP** |
| [2 — Advanced Modeling](roadmap/phase-2-advanced-modeling.md) | Cover most maker and product-design parts | M2.1 – M2.9 | v0.2 – v0.5 |
| [3 — Assemblies & Output](roadmap/phase-3-assemblies-and-output.md) | Multi-part designs, motion, drawings, scripting, blocks | M3.1 – M3.7 | **v1.0** |
| [Future](roadmap/future.md) | Ideas without a schedule, and explicit non-goals | — | — |

Related documents: [Architecture](ARCHITECTURE.md) · [Platforms & distribution](PLATFORMS.md) · [Prior art & lessons](PRIOR-ART.md) · [Building](BUILDING.md)

**Current status:** Phase 0 is done and tagged as the **v0.0.1** tech preview; the app also runs on a real iPad. Phase 1 has started with M1.1 (document, timeline & undo): the apps open documents through the core's C API.

---

## Vision

Facetry is a parametric 3D CAD app for iPad, Mac and the web.

- **Fusion 360's way of thinking:** sketches with constraints, a feature timeline you can roll
  back and edit, components and bodies, user parameters.
- **Shapr3D's way of touching:** select something and the relevant tools appear next to it,
  push and pull faces directly, draw with the Apple Pencil, navigate with your fingers.
- **Blocks for beginners:** a Scratch-style block editor for people with little CAD or coding
  experience. It builds real, editable models with sliders for every value.
- **One core, three platforms:** the same modeling core and the same file format run natively
  on iPad and Mac and as WebAssembly in the browser.

## Product principles

1. **Direct manipulation, recorded parametrically.** Every push, pull or drag creates a normal,
   editable feature in the timeline. There is no separate "direct mode".
2. **Pencil-first, not pencil-only.** Pencil and fingers on iPad, keyboard and mouse on Mac, and
   both on the web are all first-class inputs. Every tool must work with each of them.
3. **Context over chrome.** Show the few actions that make sense for the current selection
   instead of a ribbon with a hundred icons. Command search and the full toolbar are always one
   step away.
4. **Familiar to Fusion users.** Fusion users should feel at home: the same concepts and names,
   and similar keyboard shortcuts.
5. **Failures are visible and recoverable.** A failed fillet marks its feature red in the
   timeline. The rest of the model keeps working.
6. **Local-first and private.** No account, no cloud requirement, no tracking. Your files are
   yours, in an open, documented format.
7. **Same documents everywhere.** A file made on the iPad opens unchanged on the web and on the
   Mac. Platform differences come only from platform capabilities (see
   [Platforms](PLATFORMS.md)).
8. **Small steps that ship.** Every milestone leaves the app in a working state.

## Milestone index

Status: ⬜ planned · 🟡 in progress · ✅ done

Size is the rough effort for one developer working with AI assistance. **S** is one working
session (a few hours), **M** two to four sessions, **L** about a week of sessions, and **XL**
several weeks.

These sizes were recalibrated in M0.7. The original estimate in weeks was far too pessimistic:
all of Phase 0 (M0.1–M0.7) took one day of sessions on 2026-10-16. From Phase 1 on, most work is
UI that the maintainer has to try on real devices, which AI does not speed up the same way. The
relative sizes stay as they were.

### Phase 0 — Foundation

| ID | Milestone | Size | Status |
|---|---|---|---|
| M0.1 | Repository & engineering setup | S | ✅ |
| M0.2 | OpenCASCADE builds for Apple and WebAssembly | M | ✅ |
| M0.3 | Core skeleton & API boundary | M | ✅ |
| M0.4 | Viewport renderer spike | M | ✅ |
| M0.5 | App shells (SwiftUI + web) | M | ✅ |
| M0.6 | Document model & persistent naming prototype | L | ✅ |
| M0.7 | Phase 0 wrap-up & tech preview | S | ✅ |

### Phase 1 — Core Modeling (MVP)

| ID | Milestone | Size | Status |
|---|---|---|---|
| M1.1 | Document, timeline & undo | L | 🟡 |
| M1.2 | Viewport, navigation & selection | M | ⬜ |
| M1.3 | Sketching: geometry, snapping & inference | L | ⬜ |
| M1.4 | Sketch constraints & dimensions | L | ⬜ |
| M1.5 | Solid features: primitives, extrude, revolve, fillet, chamfer, combine | L | ⬜ |
| M1.6 | Direct manipulation: press/pull, move, context toolbar | M | ⬜ |
| M1.7 | Construction geometry & inspection | M | ⬜ |
| M1.8 | Pencil, touch & keyboard interaction | L | ⬜ |
| M1.9 | Import, export & files | M | ⬜ |
| M1.10 | MVP release v0.1 | M | ⬜ |

### Phase 2 — Advanced Modeling

| ID | Milestone | Size | Status |
|---|---|---|---|
| M2.1 | Parameters & design intent | M | ⬜ |
| M2.2 | Patterns & mirror | M | ⬜ |
| M2.3 | Advanced sketching | L | ⬜ |
| M2.4 | Advanced solid features (shell, loft, sweep, hole, thread, …) | XL | ⬜ |
| M2.5 | Direct editing II (delete/move faces, edit imported bodies) | L | ⬜ |
| M2.6 | Components, appearances & materials | M | ⬜ |
| M2.7 | Inspection & visualization | M | ⬜ |
| M2.8 | Interop II (mesh import, DXF/SVG, USDZ, glTF) | M | ⬜ |
| M2.9 | Performance & robustness | L | ⬜ |

### Phase 3 — Assemblies & Output

| ID | Milestone | Size | Status |
|---|---|---|---|
| M3.1 | Assemblies & joints | XL | ⬜ |
| M3.2 | 2D drawings | XL | ⬜ |
| M3.3 | Surface modeling | L | ⬜ |
| M3.4 | Sheet metal (stretch goal) | XL | ⬜ |
| M3.5 | Scripting API | L | ⬜ |
| M3.6 | Blocks: visual modeling for beginners | L | ⬜ |
| M3.7 | Toward 1.0 | L | ⬜ |

## Benchmark models

Each phase is accepted with a set of reference parts. They must be buildable on every platform
using only the app's UI. Their documents become automated regression tests in `tests/models/`:
the test regenerates each document and checks volume, area, bounding box and topology counts.

**Phase 0:** a box with one filleted edge whose fillet survives a change to the box width.

**Phase 1:**
1. **L-bracket:** constrained L profile, extruded, two through-holes, inner edge filleted.
2. **Knob:** revolved profile, chamfered top, D-shaped shaft hole cut.
3. **Phone stand:** fully constrained side profile, extruded, all outer edges filleted.
4. **Simple box with pocket:** box with a pocket cut from an offset sketch on the top face and
   filleted vertical edges.
5. **Edit test:** change the first sketch dimension of models 1–4. All later features must
   regenerate correctly, without fillets jumping to the wrong edge.

**Phase 2:**
1. **Electronics enclosure:** shelled box with a lid lip, screw bosses placed with a pattern,
   countersunk holes, all sizes driven by user parameters.
2. **Bottle:** lofted body, shelled, with a modeled thread at the neck.
3. **Gear-like wheel:** circular pattern of cut features, mirrored hub.
4. **Name plate:** embossed text on a filleted plate.
5. **STEP edit:** imported STEP part with a hole resized and a fillet removed by direct editing.

**Phase 3:**
1. **Hinge:** two components joined with a revolute joint with limits, animated.
2. **Bracket drawing:** drawing of the Phase 1 L-bracket with three views, a section and
   dimensions, exported as a PDF.
3. **Sheet-metal bracket** (if M3.4 lands): bent bracket with a flat pattern exported as a DXF.
4. **Blocks storage box:** a box with N × M compartments and a lid, built only with blocks. Width,
   depth, compartment count and wall thickness are sliders.

## Release plan

| Version | Contents | Channels |
|---|---|---|
| v0.0.1 | Phase 0 tech preview: rotate and select a filleted box | Web |
| v0.1 | Phase 1 complete: MVP | Web, macOS (GitHub Releases), iPadOS (TestFlight → App Store) |
| v0.2 | M2.1 – M2.3: parameters, patterns, advanced sketching | all |
| v0.3 | M2.4: advanced solid features | all |
| v0.4 | M2.5 – M2.7: direct editing, components, inspection | all |
| v0.5 | M2.8 – M2.9: interop and performance | all |
| v0.6 – v0.9 | Phase 3 milestones as they land | all |
| v1.0 | Phase 3 complete, file format stability guarantee | all |

The web build is deployed from `main` continuously, so it always shows the latest merged state.
Native builds are tagged releases.

## Risks

| Risk | Impact | Mitigation |
|---|---|---|
| **Topological naming:** edits to early features make later features reference the wrong face or edge | Parametric editing becomes unreliable | Solved first in M0.6, before any feature work. Benchmark edit tests are automated, and a repair UI handles broken references |
| **OpenCASCADE robustness** (complex fillets, offsets, direct face edits) | Some operations fail where commercial kernels succeed | Clear error states per feature, fallbacks (e.g. press/pull as extrude + boolean), OCCT issues tracked and reported upstream |
| **WebAssembly size and memory** (OCCT is large, wasm32 has a 4 GB limit, Safari on iPad is stricter) | Slow first load, crashes on large models in the browser | Minimal OCCT module set, lazy-loaded modules (e.g. STEP), size budgets in CI, native-only marks where needed |
| **Two UI shells** (SwiftUI and web) | Double UI work, features drift apart | Keep logic in the shared core, including tool state machines, snapping and selection logic. The shells only render panels and forward input |
| **Sketch solver UX** | Sketching feels sticky or unpredictable | Use a proven solver (PlaneGCS). Dedicated milestone for inference and dragging. Compare behavior side by side with Fusion |
| **OCCT build complexity and CI time** | Slow CI, hard onboarding | Prebuilt, versioned OCCT artifacts. Normal CI never rebuilds OCCT |
| **LGPL compliance** (OCCT, PlaneGCS) in App Store and web builds | Legal issues with distribution | Dynamic frameworks on Apple. The whole project is open source and can be rebuilt. Third-party notices ship in the app. License review before the first App Store submission (M1.10) |
| **AI-generated code quality** | Hidden bugs, inconsistent architecture | Small PRs, ADRs for decisions, golden-model tests, CI on every PR, the architecture rules in `CLAUDE.md` |
| **Scope creep** | Nothing ever ships | Milestones are small and shippable. Anything not in a milestone goes to [Future](roadmap/future.md) |

## Maintaining this roadmap

- The roadmap lives in the repository and changes only through pull requests, like code.
- Each milestone has a matching GitHub milestone, and work items become issues linked to it.
- When a feature lands, its checkbox in the phase document is ticked in the same PR.
- Decisions that change the plan are recorded as ADRs in `docs/adr/` (introduced in M0.1).
