# ADR-0001: Geometry kernel: OpenCASCADE

- **Status:** Accepted
- **Date:** 2026-10-16
- **Milestone:** M0.1

## Context

Facetry is a parametric solid modeler in the style of Fusion 360. It needs a boundary
representation (B-Rep) kernel: exact faces and edges, robust booleans, fillets and chamfers on
edges, shell, offset, loft and sweep, and STEP import/export.

Constraints:

- It must run natively on iPadOS and macOS and compile to WebAssembly.
- The license must allow distribution in the App Store and as an open-source project.
- No budget for commercial licenses.
- Writing a kernel ourselves is not realistic. Robust booleans and fillets take commercial teams
  decades.

## Options

### OpenCASCADE Technology (OCCT)

- Mature open-source B-Rep kernel: booleans, fillets, chamfers, offsets, shell, loft, sweep,
  defeaturing, hidden-line removal, meshing, shape healing, STEP/IGES.
- Used by FreeCAD, CadQuery/build123d, Dune 3D and several web CAD projects, so it is proven
  both natively and as WebAssembly.
- LGPL-2.1 with an additional exception.
- Weaker than commercial kernels for complex fillets and direct face edits. The C++ API is old
  and large, and the build is heavy.

### Parasolid (Siemens) or ACIS / ShapeManager

- The kernels behind Shapr3D, Onshape and Fusion 360. Best robustness, especially for fillets
  and direct editing.
- Commercial licenses with fees only available on request, and no open-source distribution.
  Not an option for this project.

### Rust kernels (truck, Fornjot)

- Modern code and easy WebAssembly builds.
- Not mature enough: missing or fragile fillets, booleans and offsets.

### Mesh or implicit kernels (Manifold, libfive)

- Very robust booleans (Manifold) or elegant modeling with implicit fields (libfive).
- No exact edges and faces: no edge fillets in the CAD sense, no STEP, no edge or face
  references for a feature timeline.

### SolveSpace's NURBS kernel

- Small and self-contained, but GPL-3.0 (conflicts with App Store distribution) and less
  robust booleans.

## Decision

Use **OpenCASCADE** as the geometry kernel. It is the only mature, open B-Rep kernel that covers
the full feature set and runs on all three platforms.

## Consequences

- All OCCT usage is wrapped in a kernel adapter inside `facetry-core`. No OCCT types leave it
  (see the [architecture rules](../ARCHITECTURE.md#rules)).
- OCCT is built once per pinned version for iOS, the iOS simulator, macOS and WebAssembly. CI
  reuses prebuilt artifacts (M0.2), see [`third_party/occt`](../../third_party/occt/README.md).

## Pinned version (M0.2)

- **OCCT 8.0.1** (tag `V8_0_1`, July 2026), the latest stable release when Facetry started.
  Starting on the 8.x line avoids an early migration from 7.x. 8.0.1 is a patch release on top
  of 8.0.0 (May 2026).
- **Toolkits:** modeling (TKPrim, TKBO, TKBool, TKFillet, TKOffset, TKFeat and their
  foundations), TKMesh, TKShHealing, TKDESTEP and TKDESTL. STEP needs TKXCAF, which in turn
  pulls in TKCAF, TKVCAF, TKService and TKV3d. They are built without OpenGL and FreeType.
- **Apple:** all toolkits linked into one dynamic `OCCT.framework` per platform, shipped as an
  XCFramework. **WebAssembly:** static libraries built with native WebAssembly exceptions and
  SIMD.
- Sizes and timings are recorded in [benchmarks](../benchmarks.md).
- LGPL compliance: OCCT is linked dynamically on Apple platforms, and the notices are shipped.
  Because Facetry is fully open source with build scripts, users can rebuild it against a
  modified OCCT.
- Every feature must handle kernel failures gracefully. Some operations need fallbacks, such as
  press/pull as extrude + boolean, because OCCT is less robust than commercial kernels there.
- Persistent naming (M0.6) is built on OCCT's operation history (generated/modified/deleted
  shapes), not on OCAF's TNaming. OCAF may still be evaluated in M0.6.
