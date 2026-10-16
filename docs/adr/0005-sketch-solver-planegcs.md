# ADR-0005: Sketch constraint solver: PlaneGCS

- **Status:** Accepted
- **Date:** 2026-10-16
- **Milestone:** M0.7

## Context

Sketches (M1.3, M1.4) need a 2D constraint solver. It has to:

- solve geometric constraints (coincident, parallel, tangent, …) and dimensions;
- re-solve while the user drags geometry, at the display's frame rate;
- diagnose the system: degrees of freedom left, plus conflicting and redundant constraints. That
  drives the Fusion-style status colors and error messages;
- run natively on iPadOS and macOS and as WebAssembly;
- have a license that works for an MIT project in the App Store, the same constraint as the
  kernel ([ADR-0001](0001-geometry-kernel-opencascade.md)).

[Prior art](../PRIOR-ART.md#sketch-solvers) lists the candidates.

## Options

### PlaneGCS (FreeCAD)

- **What it is:** FreeCAD's 2D solver, in C++ on Eigen.
  - Solvers: DogLeg (default), Levenberg-Marquardt and BFGS.
  - Splits a sketch into independent parts.
  - Drag solving through subsystems.
  - QR-based diagnosis: degrees of freedom, conflicting and redundant constraints.
- **Maturity:** maintained, and used for years in FreeCAD. Outside FreeCAD there is a WebAssembly
  build (`@salusoft89/planegcs`) and Python bindings.
- **Drawbacks:**
  - Written for FreeCAD's build: it includes FreeCAD headers and Boost.Graph.
  - Dense QR diagnosis gets slow for very large sketches.
  - LGPL-2.1-or-later.

### SolveSpace's solver (libslvs)

- Proven and compact, with good ideas: substitution, scaled drag parameters, culprit search.
- GPL-3.0, so it is not compatible with MIT plus App Store distribution. Ideas only.

### Our own solver on Eigen

- Full control and MIT.
- Newton/least-squares solving is the easy part. Robust diagnosis, decomposition and drag
  behavior are what take years to get right. Months of work before the first constrained
  sketch.

### The JavaScript wrapper `@salusoft89/planegcs`

- Ready-made for the web, but web-only. The native apps would need a different solver path,
  against the shared-core principle ([ADR-0002](0002-shared-core-with-native-and-web-shells.md)).

### D-Cubed 2D DCM (Siemens)

- The solver behind Fusion 360 and Onshape. Commercial, not an option.

## Spike

PlaneGCS from FreeCAD **1.1.4** was compiled unchanged, with Eigen 3.4.0. Shim headers stood in
for what FreeCAD provides:

- an empty export macro;
- `FCConfig.h`;
- `Base::Console` (logging only);
- the four Boost.Graph functions it uses to find connected components;
- a forced `#include <cassert>`.

The test solved *n* rectangles, each with horizontal/vertical constraints, two dimensions and a
fixed corner, from a rough start. Measured on the M0.2 benchmark machine (Apple M3):

| | Native (clang -O2) | WebAssembly (Node 24, -O3) |
|---|---|---|
| 1 rectangle: solved exactly, 0 degrees of freedom | ✅ | ✅ |
| Conflicting dimension added: solve fails, the 4 involved constraints are reported | ✅ | ✅ |
| 50 rectangles (400 parameters): first solve + diagnosis | 36 + 34 ms | 77 + 45 ms |
| 50 rectangles: re-solve after a dimension change | 0.3 ms | 0.5 ms |
| 250 rectangles (2000 parameters): first solve + diagnosis | 235 + 440 ms | 539 + 394 ms |
| 250 rectangles: re-solve after a dimension change | 1.9 ms | 1.9 ms |
| Size | — | 0.29 MB raw, 0.08 MB brotli, including its share of the C++ runtime |

`std::async`, which PlaneGCS uses for diagnosis, runs deferred in the single-threaded WebAssembly
build without changes.

## Decision

Use **PlaneGCS**, vendored as C++ into the core. Not the JavaScript wrapper.

- **Sources:** pinned to a FreeCAD release tag (now 1.1.4) and kept unmodified in
  `third_party/planegcs/`. FreeCAD's dependencies are replaced by the shim headers from the
  spike. If a source change is ever unavoidable, it lives in a `.patch` file next to the pin.
- **Eigen:** 3.4.0, header-only, MPL-2.0, built with `EIGEN_MPL2_ONLY`.
- **Library:** PlaneGCS builds as its own library, `facetry-planegcs`. Only a sketch bridge in
  the core (`core/src/sketch/`) sees its types, the same way OCCT stays inside the kernel
  adapter.
- **Document model:** the sketch model stores its own IDs for points, curves and constraints
  ([ADR-0006](0006-persistent-naming.md)) and never PlaneGCS indices, so the solver could be
  replaced.

## Consequences

- **Licensing:**
  - PlaneGCS is LGPL-2.1-or-later like OCCT, so it adds no new kind of obligation.
  - It is linked the same way as OCCT: a dynamic framework on Apple, static in WebAssembly.
  - All source and build scripts are public.
  - Its notice goes into `THIRD_PARTY_NOTICES.md` when it is vendored (M1.4).
  - The license review before the first App Store submission (M1.10) covers both libraries.
- **Performance:**
  - Solving runs in the core, off the UI thread: in the Web Worker on the web, on a background
    task natively.
  - Re-solving while dragging is in the low milliseconds, even for very large sketches.
  - Diagnosis of large sketches takes hundreds of milliseconds. It runs after an edit, not on
    every drag frame, and is cancelled when a new edit arrives.
- **Updates:** move the pinned tag and re-run the solver tests. The shims must stay small. If
  FreeCAD starts depending on more of its own code, that needs a new decision.
- **Known PlaneGCS issues to cover with tests:** some non-driving dimensions misbehave, and
  sparse QR misdetects rank on some shapes above about 1000 parameters.
- **Ideas from SolveSpace** (dragged-parameter weighting, a time-boxed search for the
  constraint to remove) can be added in the bridge without touching PlaneGCS.
