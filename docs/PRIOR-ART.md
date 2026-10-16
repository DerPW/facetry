# Prior art & lessons learned

Facetry is not the first attempt at an approachable, parametric CAD tool, and it builds on the
same kernel as several open projects. This document collects what others have solved, what went
wrong for them, and what that means for Facetry. Proven approaches can then be reused instead of
rediscovered.

- **As of 2026-10-16.** Projects move fast. Every claim links to a primary source where possible;
  check the source before relying on a detail.
- **Product behavior only.** For commercial tools (Fusion 360, Shapr3D, Onshape, Plasticity) this
  describes publicly documented behavior, not internals.
- **Suggestions, not decisions.** The [lessons table](#lessons-for-facetry) maps findings to
  roadmap milestones. Decisions still go through ADRs and the roadmap.

See also: [Roadmap](ROADMAP.md) · [Architecture](ARCHITECTURE.md) · [Platforms](PLATFORMS.md)

## At a glance

| Project | What it is | License | Status (Oct 2026) | Most useful lesson for Facetry |
|---|---|---|---|---|
| [FreeCAD](https://github.com/FreeCAD/FreeCAD) | Open-source parametric CAD on OCCT | LGPL-2.1+ | Active (1.1.4, 26.3 RC) | How to fix topological naming, and what it costs to retrofit |
| [Onshape](https://www.onshape.com/) | Cloud CAD, FeatureScript | Commercial | Active | References as queries against feature history |
| [build123d](https://github.com/gumyr/build123d) | Python code-CAD on OCCT 8.0.1 | Apache-2.0 | Active (0.13) | Reading OCCT's operation history; geometric selectors |
| [CadQuery](https://github.com/CadQuery/cadquery) | Python code-CAD on OCCT | Apache-2.0 | Active (2.8) | String selectors; experimental history API |
| [Dune 3D](https://github.com/dune3d/dune3d) | Desktop parametric CAD, OCCT + SolveSpace solver | GPL-3.0 | Active (1.4) | Tight scope lets one developer ship fast |
| [SolveSpace](https://github.com/solvespace/solvespace) | Parametric CAD with its own solver | GPL-3.0+ | Active (3.2) | Solver techniques (reference only, GPL) |
| [PlaneGCS](https://github.com/FreeCAD/FreeCAD/tree/main/src/Mod/Sketcher/App/planegcs) | FreeCAD's 2D sketch solver | LGPL-2.1+ | Active | The solver candidate for ADR-0005 |
| [libcascade](https://github.com/taucad/opencascade.js) | OCCT 8.0.1 → WebAssembly (fork of opencascade.js) | LGPL-2.1 | Active (3.1) | Same wasm build approach as Facetry, plus extra tuning |
| [occt-wasm](https://github.com/andymai/occt-wasm) | OCCT → WebAssembly with a handle-based API | MIT/Apache + LGPL | Active (5.7) | Error codes and handles across the wasm boundary; LTO pitfalls |
| [replicad](https://github.com/sgenoud/replicad) | TypeScript code-CAD on OCCT wasm | MIT + LGPL | Active (1.1) | Face/edge "finders" as a selection API |
| [CascadeStudio](https://github.com/zalo/CascadeStudio) | Browser code-CAD IDE on OCCT wasm | MIT | Active | Worker architecture, sliders, URL sharing |
| [Chili3D](https://github.com/xiangechen/chili3d) | Interactive browser CAD on OCCT wasm | AGPL-3.0 | Active (0.7) | Kernel behind interfaces; visual-programming plugin |
| [opencascade.js](https://github.com/donalffons/opencascade.js) | The original OCCT → WebAssembly port | LGPL-2.1 | Dormant since 2023 | Bind only what you need; size tricks |
| [Manifold](https://github.com/elalish/manifold) | Robust mesh boolean library | Apache-2.0 | Very active (3.5) | A fast mesh companion, not a B-Rep kernel |
| [truck](https://github.com/ricosjp/truck) | Rust B-Rep kernel | Apache-2.0 | Active on master; last release 2024 | Not mature enough yet |
| [Fornjot](https://github.com/hannobraun/fornjot) | Rust B-Rep kernel/CAD | 0BSD | **Archived June 2026** | Why kernel projects stall |
| [Ondsel](https://www.ondsel.com/blog/goodbye/) | Company polishing FreeCAD | — | **Shut down Oct 2024** | Why UX-first CAD companies fail |
| Fusion 360 | Commercial parametric CAD | Commercial | Active | The workflow Facetry follows |
| Shapr3D | Commercial iPad-first CAD on Parasolid | Commercial | Active | The interaction Facetry follows, and its gaps |
| Plasticity | Commercial direct NURBS modeler on Parasolid | Commercial | Active | Keyboard speed, command palette |

---

## Persistent naming

Relevant for: **M0.6** (persistent naming prototype, ADR-0006).

### FreeCAD: the topological naming problem and its fix

FreeCAD features referred to sub-shapes by type and index (`Face6`). OCCT renumbers sub-shapes
after upstream edits, so sketches, fillets and attachments jumped to the wrong element or broke.
The problem was tracked from 2012 and was only fixed in FreeCAD 1.0 (November 2024), about twelve
years later.

realthunder's (Zheng Lei) solution gives every shape an **element map** from stable *mapped
names* to the current indexed names
([algorithm](https://github.com/realthunder/FreeCAD_assembly3/wiki/Topological-Naming-Algorithm)):

- Names are derived from the history that OCCT's makers report (`Modified`/`Generated`). Unchanged
  elements carry over their names, modified and generated elements are named after their sources,
  and the rest are named from their parents or children.
- A name like `Face6;:M2;FUS;:T1:5:F` encodes the source element, the kind of change, the operation
  and the owner. A string hasher shortens long names.
- References store the mapped name plus a "shadow" indexed name as a fallback.
- The fix was merged in phases ([#8432](https://github.com/FreeCAD/FreeCAD/issues/8432)) and
  switched on by [#13705](https://github.com/FreeCAD/FreeCAD/pull/13705).

Known limits:
- The index in a name (`M2`) still follows OCCT's output order. Splits into three or more pieces
  stay ambiguous.
- Recovering a broken reference is heuristic.
- It costs performance: on realthunder's test model, recompute went from 40 s to 52 s and the file
  from 17.8 to 22.6 MiB.
- Changing a feature's algorithm changes its names, so names need a per-feature version.

Context: a FreeCAD PartDesign *Body* is meant to hold one contiguous solid, and sketches *attach*
to faces or datum planes. Attaching to faces was the classic victim, and the common workaround was
attaching to origin planes.

### Onshape: references as queries

In [FeatureScript](https://cad.onshape.com/FsDoc/library.html), every operation has a
hierarchical id. References are **queries** that are re-evaluated against the model, such as
`qCreatedBy(id, EntityType)` or entities tracked from a sketch line, rather than stored entities
([modeling docs](https://cad.onshape.com/FsDoc/modeling.html)):

- Transient results of evaluating a query are kept apart from persistent queries.
- Loop indices can be marked unstable, and there are explicit disambiguation helpers.
- In the feature list, a broken reference fails the features that use it. **Replace reference**
  on the first failing feature fixes it downstream
  ([repairing](https://cad.onshape.com/help/Content/Document/repairing.htm)).

### build123d and CadQuery: geometric selection

- [CadQuery selectors](https://cadquery.readthedocs.io/en/latest/selectors.html) (`>Z`, `|Z`,
  `%Plane`, combined with `and`/`or`/`not`) are purely geometric and re-run every time. CadQuery
  2.8 adds an experimental history API.
- [build123d](https://build123d.readthedocs.io/en/latest/topology_selection.html) sorts, groups
  and filters shape lists by geometry. Since 0.12 its `Select.NEW`/`LAST` read **OCCT's own
  operation history**, and 0.13 runs on OCCT 8.0.1, the same version as Facetry. Its docs advise
  against fixed list indices.
- Code-CAD can rely on the author editing the script when a selection breaks. A GUI app cannot.
  Geometric predicates are still excellent **tiebreakers** and a good **scripting API**.

### OCAF TNaming and OCCT 8 BRepGraph

OCCT ships its own naming framework in
[OCAF](https://dev.opencascade.org/doc/overview/html/occt_user_guides__ocaf.html)
(`TNaming_NamedShape`, `TNaming_Selector`). It records shape evolutions (generated, modified,
deleted) and re-finds selections after changes. It requires OCAF's document model and strict
rules for every algorithm. FreeCAD did not adopt it. Facetry's own JSON document model doesn't fit
it either, but its naming types (generation, intersection, filter by neighbors) are a useful
vocabulary.

OCCT 8.0 adds `BRepGraph` with typed node ids and history tracking
([8.0 release](https://github.com/Open-Cascade-SAS/OCCT/releases/tag/V8_0_0)). Evaluated in
M0.6 ([ADR-0006](adr/0006-persistent-naming.md)): its IDs are stable inside one graph, but a model
rebuilt from changed parameters is a new graph, so it does not replace naming.

### Dune 3D: what happens without naming

[Dune 3D](https://docs.dune3d.org/en/latest/groups.html) stores fillet and chamfer edges as
`TopExp_Explorer` indices, without naming protection. Its documentation therefore recommends
putting fillets and chamfers last. That is fine for a tool with a narrow scope, but not for
Facetry's timeline editing.

---

## Sketch solvers

Relevant for: **ADR-0005**, **M1.3**, **M1.4**.

### PlaneGCS (FreeCAD)

- A 2D solver over a flat parameter vector, built on Eigen. It offers DogLeg (default),
  Levenberg-Marquardt and BFGS
  ([GCS.h](https://github.com/FreeCAD/FreeCAD/blob/main/src/Mod/Sketcher/App/planegcs/GCS.h)).
- **Dragging:** temporary constraints switch to a two-subsystem solve.
- **Decomposition:** the sketch is split into independent parts before solving.
- **Diagnosis:**
  - A QR decomposition of the Jacobian gives the degrees of freedom (parameters minus rank).
  - It also gives lists of **conflicting**, **redundant** and **partially redundant**
    constraints. These are exactly what a UI needs for Fusion-style status coloring and error
    messages.
  - Above about 1000 parameters it switches to a sparse QR, which a source comment says
    misdetects rank on some shapes.
- **Used outside FreeCAD:**
  - [@salusoft89/planegcs](https://github.com/Salusoft89/planegcs) is a maintained WebAssembly
    build (Emscripten 6, B-splines since 1.2). Its README notes that some non-driving dimensions
    misbehave.
  - Python bindings exist ([planegcs on PyPI docs](https://planegcs.readthedocs.io/en/stable/api.html)).

### SolveSpace (reference only)

SolveSpace's solver ([system.cpp](https://github.com/solvespace/solvespace/blob/master/src/system.cpp))
works like this:

1. It builds symbolic expressions and derives the Jacobian symbolically.
2. It solves trivial equalities by substitution first.
3. It runs a Newton method with a least-squares step, scaling dragged parameters so they move
   first.
4. On failure, a **time-boxed search** removes one constraint at a time to name the culprits.

It is GPL-3.0, which conflicts with Facetry's MIT license and App Store distribution, so it is a
source of ideas only. Dune 3D vendors it directly and patches it for speed.

### What to avoid

Chili3D's sketch solver ships only as a prebuilt binary under a separate license, without source.
A closed piece at the center of an open CAD app is not an option for Facetry.

---

## OpenCASCADE on the web

Relevant for: **M0.2** (done), **M0.3**, **M0.5**, **M2.9**.

### Facetry's WebAssembly build is now the common approach

Two actively maintained projects compile OCCT 8.0.1 with the same essentials as Facetry: native
WebAssembly exceptions (`-fwasm-exceptions`), SIMD and **`OCC_CONVERT_SIGNALS` removed**.

| Project | OCCT / Emscripten | Notable choices |
|---|---|---|
| [libcascade](https://github.com/taucad/opencascade.js) | 8.0.1 / 6.0.5 | mimalloc, Closure, `wasm-opt -O4 --converge`, 8 MB stack, single- and multi-threaded builds with provenance files. replicad builds on it |
| [occt-wasm](https://github.com/andymai/occt-wasm) | 8.0.0p1 fork / 5.0.3 | `-mtail-call -O3 -sEVAL_CTORS=2`; integer **handles** and an **error-code enum** across the boundary; kernel in a worker via Comlink; about 4.5 MB brotli. **Dropped `-flto` after an LTO miscompile** corrupted the heap. Restores the stack after a caught exception |

Background on the signals problem:
- An [OCCT forum thread from 2022](https://occt3d.com/dev/content/test-fwasm-exception-crash/index.html)
  documents the same setjmp/exception conflict with `-fwasm-exceptions`, solved the same way.
- OCCT's own [wasm build script](https://github.com/Open-Cascade-SAS/OCCT/blob/V8_0_1/adm/scripts/wasm_build.sh)
  uses JavaScript-based exceptions plus Emscripten's setjmp, a slower combination that works. It
  has no wasm CI.
- Emscripten 6.0.12 still defaults to legacy wasm exceptions. Standard exceptions (`exnref`, via
  `-sWASM_LEGACY_EXCEPTIONS=0`) need Chrome 137, Firefox 131, Safari 18.4 or Node 24.15
  ([ChangeLog](https://github.com/emscripten-core/emscripten/blob/main/ChangeLog.md)). Legacy
  wasm exceptions work back to Safari 15.2.

### Other web ports

- **[opencascade.js](https://github.com/donalffons/opencascade.js)** (dormant since 2023):
  - Generates Embind bindings for almost all of OCCT.
  - Shows how much **binding only the symbols you use** saves: 9.1 MB brotli for the full build vs
    2.4 MB for a custom build
    ([file size docs](https://github.com/donalffons/opencascade.js/blob/master/website/docs/02-getting-started/03-file-size.md)).
- **[replicad](https://github.com/sgenoud/replicad):**
  - A drawing "pen" API and [finders](https://replicad.xyz/docs/tutorial-overview/finders)
    (`inPlane`, `parallelTo`, `ofSurfaceType`, `containsPoint`, …, combined with `and`/`not`).
  - The viewer **highlights what a finder matches**.
  - The kernel runs in a worker via Comlink. Manifold is an optional mesh backend.
- **[CascadeStudio](https://github.com/zalo/CascadeStudio):**
  - The kernel runs in a Web Worker and caches results by argument hash.
  - GUI sliders are declared in code, models are shared by URL, and it installs as a PWA.
  - A small global API lets scripts and agents drive it.
- **[Chili3D](https://github.com/xiangechen/chili3d):**
  - An interactive (not code) browser CAD with a ribbon.
  - The kernel sits behind interfaces, so it can be swapped.
  - It offers snapping and tracking, live boolean preview, URL-loaded plugins, and a Rete.js
    **visual-programming** plugin.
  - Its kernel runs on the main thread.

### Size comparison

| Build | Download (brotli) |
|---|---|
| Facetry, modeling only (M0.2) | 2.58 MB |
| Facetry, modeling + STEP (M0.2) | 3.33 MB |
| occt-wasm | ~4.5 MB |
| opencascade.js custom build (docs site) | ~2.4 MB |
| opencascade.js full build | 9.1 MB |

Facetry's static libraries plus linker dead-code elimination already land in the same range as
hand-tuned builds.

---

## Kernels Facetry did not choose

Relevant for: **ADR-0001**, risk register.

- **Parasolid.** Shapr3D started on OpenCASCADE and **switched to Parasolid in version 3.0
  (December 2017) for robustness** ([Siemens case study](https://resources.sw.siemens.com/en-US/case-study-shapr3d)).
  Plasticity likewise moved from the C3D kernel to Parasolid. This is the clearest warning about Facetry's biggest
  technical risk: OCCT robustness in fillets and direct edits. It supports the plan of explicit
  error states, fallbacks and a kernel adapter that keeps OCCT types contained.
- **truck** ([repo](https://github.com/ricosjp/truck)) has NURBS B-Rep, booleans, STEP and recent
  work on fillets and offsets. The last published release is from September 2024, boolean fixes
  live only on master, and booleans on coincident geometry are still reported to fail.
- **Fornjot** was **archived in June 2026**
  ([shutting down](https://www.fornjot.app/blog/shutting-down-fornjot/)). The author's reasons:
  - progress was too slow, and B-Rep was harder than early success suggested;
  - sponsors were paying for a vision before there was a product;
  - scope was split between kernel and app;
  - incremental work found problems too late.
- **CADmium** (browser CAD on truck) was archived in 2025.
- **Manifold** ([repo](https://github.com/elalish/manifold)) does guaranteed-manifold mesh
  booleans, fast, with a roughly 0.5 MB wasm build. OpenSCAD, Blender and Godot use it. It has no
  exact geometry, fillets or STEP, so it cannot replace a B-Rep kernel. It is a strong candidate
  for **mesh bodies** (M2.8) and fast previews.

---

## Interaction design

Relevant for: **M1.1**, **M1.2**, **M1.6**, **M1.8**.

### Fusion 360

- **Marking menu:** right-click opens a radial menu with eight wedges.
  - The defaults are Repeat (top), Undo, Redo, Sketch (opens a second ring), Delete, Press Pull,
    Move/Copy and Hole. A normal context menu sits below it.
  - While a command runs, two wedges become OK/Cancel. Flicking works without waiting for the
    menu to appear.
  - Source: [Autodesk learning](https://www.autodesk.com/learn/ondemand/curated/fusion-360-ui-general/2KoHNZ54W5z9FfRaCLQIcv).
- **S toolbox:** a small search panel at the cursor, with pinnable commands that differ between
  sketch and model ([tip](https://www.autodesk.com/products/fusion-360/blog/quick-tip-the-s-key/)).
- **Press Pull (Q)** picks the operation from the selection: a profile extrudes, an edge fillets,
  a face offsets. It also edits existing fillets and chamfers
  ([help](https://help.autodesk.com/cloudhelp/ENU/Fusion-Model/files/GUID-02F9ADA3-7556-42A9-8AD1-552728D537AB.htm)).
  Users report that the automatic "new offset vs modify existing" choice surprises them.
- **Timeline** ([help](https://help.autodesk.com/cloudhelp/ENU/Fusion-Designs/files/ASM-USE-TIMELINE.htm)):
  - New features are inserted at the rollback marker.
  - Features can be reordered, suppressed (dependents too) and grouped.
- **Modeling modes:** Fusion also has a direct mode without history. Turning history back on
  collapses the geometry into a non-parametric **Base Feature**
  ([modes](https://help.autodesk.com/cloudhelp/ENU/Fusion-Designs/files/ASM-DESIGN-MODELING-MODES.htm)).
- **Sketch colors:** blue means under-constrained, black fully constrained. Forum reports show the
  coloring sometimes goes stale until a recompute.
- **Shortcuts** ([list](https://www.autodesk.com/shortcuts/fusion-360)): L line, R rectangle,
  C circle, D dimension, T trim, P project, X construction, E extrude, Q press pull, F fillet,
  M move, J joint, S toolbox. These match the plan in M1.8.

### Shapr3D

- **Input split:** fingers navigate. The Pencil draws, edits and selects without button presses
  ([Develop3D review](https://develop3d.com/reviews/review-shapr3d-ipad-parasoild-modelling/)).
- **Adaptive UI** (2020): the tool menu shows only tools valid for the selection
  ([announcement](https://discourse.shapr3d.com/t/ipad-adaptive-ui-the-key-to-quick-streamlined-modeling/7942)).
- **History-based parametric modeling** since April 2024
  ([blog](https://www.shapr3d.com/blog/shapr3d-history-based-parametric-modeling)):
  - Steps can be reordered and edited.
  - Selecting geometry filters the history to related steps.
  - Broken references are flagged.
  - Direct moves are recorded as steps. This matches Facetry's "direct manipulation, recorded
    parametrically" principle.
- **Gaps that are Facetry's opportunities:**
  - Shapr3D does not recognize freehand strokes. Users request scratch-to-delete gestures.
  - Shapr3D does not officially support Apple Pencil Pro squeeze or barrel roll
    ([forum](https://discourse.shapr3d.com/t/apple-pencil-pro-support/33643/3)).
- **Criticism** in reviews: price with no hobby tier, weak assemblies, large models and advanced
  surfacing ([G2](https://www.g2.com/products/Shapr3D/reviews)).

### Onshape on iPad

Onshape's [mobile engineering post](https://www.onshape.com/it/blog/under-the-hood-cad-for-mobile)
explains its native app with shared C++ graphics, and
[gestures](https://www.onshape.com/en/resource-center/tech-tips/tech-tip-mastering-gestures-mobile)
documents the touch design:
- A **precision selector**: long-press shows a crosshair offset from the finger.
- An automatic rotation lock while sketching.
- Two-finger tap for the context menu, double-tap to clear the selection.

### Plasticity

Plasticity is a keyboard-driven direct modeler
([docs](https://doc.plasticity.xyz/plasticity-essentials/plasticity-interface/command-palette)):
- Keys 1–4 switch the selection mode (point, edge, face, solid).
- **F** opens a searchable command palette, and any command can get a shortcut.
- Selecting edges makes fillet available immediately. The drag direction switches between
  fillet and chamfer.
- Values can be typed during commands.
- Navigation presets mimic Blender, Maya, Fusion and Rhino.

### Apple Pencil and web input facts

- **Hover:** M2-or-newer iPad Pro, iPad Air M2 or newer, and iPad mini (A17 Pro), with Apple
  Pencil 2, Pro or USB-C. API: `UIHoverGestureRecognizer` with `zOffset`, altitude and azimuth
  ([Apple](https://developer.apple.com/documentation/uikit/adopting-hover-support-for-apple-pencil)).
- **Apple Pencil (USB-C)** has no pressure, double-tap, squeeze or barrel roll
  ([comparison](https://www.apple.com/apple-pencil/)). Facetry must work fully without them.
- **Double-tap and squeeze:** `UIPencilInteraction` (iPadOS 17.5) with `didReceiveTap` and
  `didReceiveSqueeze`. `preferredTapAction` and `preferredSqueezeAction` read the user's system
  settings, and the hover pose can anchor a palette at the Pencil tip
  ([Apple](https://developer.apple.com/documentation/uikit/uipencilinteraction)).
- **Barrel roll:** `UITouch.rollAngle` (iPadOS 17.5)
  ([Apple](https://developer.apple.com/documentation/uikit/uitouch/rollangle)).
- **Low latency:** CAD sketching should use raw `UITouch` events with coalesced and **predicted**
  touches, not PencilKit, which is built for ink
  ([Apple](https://developer.apple.com/documentation/uikit/minimizing-latency-with-predicted-touches)).
- **Scribble:** `UIScribbleInteraction` gives handwriting in text fields for free. It should be
  disabled near the canvas
  ([Apple](https://developer.apple.com/documentation/uikit/uiscribbleinteraction)).
- **Safari on iPad:**
  - Pointer Events report `pen` with pressure and tilt, and Pencil hover since Safari 16.1
    ([WebKit](https://webkit.org/blog/13399/webkit-features-in-safari-16-1/)).
  - `twist` (roll) is not exposed, and there is no squeeze or double-tap on the web
    ([WebKit bug](https://bugs.webkit.org/show_bug.cgi?id=296943)).
  - Coalesced events arrived in iOS Safari 18.2.

---

## Why open CAD efforts stall

Relevant for: how Facetry is run.

- **Ondsel** tried to polish FreeCAD into a commercial product with a cloud service and shut down
  in October 2024 ([goodbye post](https://www.ondsel.com/blog/goodbye/)). Its reasons:
  - no product-market fit, and conflicting feedback from about 100 user interviews;
  - incumbents entrenched through schools;
  - trying to fix UX, add features and find a market at the same time.

  Its [one-year review](https://www.ondsel.com/blog/ondsel-365/) also notes that UI debates in
  the community turned emotional. People asked users *how* to build a feature instead of *what*
  they wanted to do. Much of its work (the Assembly workbench, sketcher improvements) lives on in
  FreeCAD.
- **Fornjot** stalled on the kernel (see [above](#kernels-facetry-did-not-choose)).
- **Dune 3D** shows the opposite: by the author's account, one experienced developer reached a
  presentable state in about three months, and 1.0 followed about three months after the public
  release ([repo](https://github.com/dune3d/dune3d)). It got there by limiting scope hard and
  reusing proven parts (OCCT, SolveSpace, Horizon EDA's editor framework).
- **The common pattern:** projects stall where the hard problem is the kernel, or where the scope
  is too wide. Facetry reuses OCCT, keeps milestones small and shippable, and records every
  scope addition in the roadmap.

---

## Lessons for Facetry

Concrete suggestions, mapped to roadmap milestones. Each still needs its own decision (ADR or
roadmap PR) before it is adopted.

| Milestone | Suggestion | From |
|---|---|---|
| M0.3 | Expose kernel objects across the wasm and Swift boundaries as integer **handles**, and failures as an **error-code enum** plus message | occt-wasm |
| M0.3 | Run the kernel in a worker behind a small message API; consider a Comlink-style RPC on the web | occt-wasm, replicad, CascadeStudio |
| M0.3 | Keep `-flto` off for OCCT; evaluate `-sEVAL_CTORS=2`, an 8 MB stack, mimalloc and `wasm-opt -O4` | occt-wasm, libcascade |
| M0.3 | Restore the wasm stack pointer after caught exceptions, if failures show up after repeated kernel errors | occt-wasm |
| M0.6 | Create names in **one wrapper around all OCCT makers**, so every feature gets naming automatically; keep an indexed shadow name for debugging | FreeCAD |
| M0.6 | Store a reference as *(creating feature, role in that feature, disambiguator)*, e.g. "side face generated from sketch edge `<uuid>`" | Onshape, FreeCAD |
| M0.6 | Use **geometric tiebreakers** (position, normal, size) when history is ambiguous, e.g. splits into three or more pieces | build123d, FreeCAD limits |
| M0.6 | Never silently rebind a broken reference; show it and offer a **repair that propagates downstream** | Onshape |
| M0.6 | Version each feature's naming algorithm, so algorithm changes don't break old files | FreeCAD |
| M0.6 | Evaluate OCCT 8's `BRepGraph` history before building everything from `Generated`/`Modified` | OCCT 8.0 |
| ADR-0005 | Vendor **PlaneGCS** C++ into the core (not the JS wrapper); keep it a separate library for LGPL | PlaneGCS |
| M1.4 | Run solver diagnosis on every edit and map its conflicting/redundant lists to constraints in the UI; make sure status colors never go stale | PlaneGCS, Fusion forum reports |
| M1.4 | Borrow solver ideas: substitution pass, weighting dragged parameters, a time-boxed "which constraint to remove" search | SolveSpace |
| M1.1 | Timeline: insert at the marker, reorder, suppress with dependents, groups, "show only failing features" | Fusion, Onshape |
| M1.1 / M1.9 | Represent imported bodies as a non-parametric **base feature** that later features build on | Fusion |
| M1.2 | Precision selector (offset crosshair on long-press) and an automatic rotation lock while sketching | Onshape |
| M1.2 | Selection modes on number keys and an "everything" mode | Plasticity |
| M1.6 | Press/Pull like Fusion, but make "new offset vs modify existing" **visible and switchable** instead of silently automatic | Fusion forum reports |
| M1.6 | Context toolbar showing only tools valid for the selection | Shapr3D, Plasticity |
| M1.8 | Raw `UITouch` with coalesced and predicted touches for sketching, not PencilKit | Apple |
| M1.8 | `UIPencilInteraction` for tap and squeeze, respecting the user's system preference; a palette anchored at the hover pose | Apple |
| M1.8 | Everything must work with the USB-C Pencil (no pressure, double-tap, squeeze, roll) and on the web (no squeeze, no roll) | Apple, WebKit |
| M1.8 | Stroke recognition and Pencil Pro gestures are **differentiators**: Shapr3D has neither | Shapr3D gaps |
| M1.8 | Marking menu defaults close to Fusion's (Repeat top, Undo/Redo left/right, OK/Cancel during commands); S/F-style command search | Fusion, Plasticity |
| M2.8 | Use Manifold for mesh bodies and fast mesh previews | Manifold |
| M3.5 | Scripting selection API modeled on finders/selectors, with on-canvas highlighting of matches | replicad, build123d |
| M3.5 / M3.6 | Sliders declared in scripts, models shareable by URL, a small automation API for agents | CascadeStudio |
| M3.6 | Look at Chili3D's Rete.js visual-programming plugin as a reference for the blocks editor | Chili3D |
| Process | Keep milestones shippable; ask users what they want to do, not how; don't split scope between kernel and app | Ondsel, Fornjot, Dune 3D |
| Risk | Treat OCCT robustness as the main technical risk (Shapr3D left OCCT for it); invest in error states and fallbacks early | Shapr3D, Plasticity |
| Legal | Review LGPL obligations for iOS and WebAssembly builds before the first App Store submission (already in M1.10) | PlaneGCS/OCCT licensing |

## Maintaining this document

- Update it when a decision draws on a new project, or when a listed project changes
  significantly (new release line, archived, license change).
- Keep claims sourced and dated. Mark uncertain points as such.
- Move a suggestion into the roadmap or an ADR when it is adopted, and link back here.
