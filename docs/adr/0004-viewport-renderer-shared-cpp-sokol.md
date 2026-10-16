# ADR-0004: Viewport renderer: one shared C++ renderer on sokol_gfx, with CPU picking

- **Status:** Accepted
- **Date:** 2026-10-16
- **Milestone:** M0.4

## Context

The viewport shows the model: shaded faces, crisp edges, vertex dots, hover and selection
highlights. Later it will also show sections, transparency, manipulators, the ViewCube and sketch
geometry. It must run with Metal on iPadOS/macOS and in the browser, at the display refresh rate
(120 Hz on ProMotion iPads) for models with around 100k triangles. It also has to answer "what is
under the finger or cursor" reliably.

Every rendering feature built twice would double the work for the rest of the project
([ADR-0002](0002-shared-core-with-native-and-web-shells.md) already keeps logic in shared C++ for
that reason).

## Options

### A: One C++ renderer on a thin graphics abstraction

The candidates:

| Library | Backends | Notes |
|---|---|---|
| [sokol_gfx](https://github.com/floooh/sokol) | Metal, WebGL2, WebGPU, GL, D3D11, Vulkan | Single header, zlib license, C API, very small. `sokol-shdc` cross-compiles annotated GLSL to MSL, GLSL ES and WGSL |
| bgfx | Many | Larger, its own build system and shader toolchain |
| Dawn / wgpu-native (WebGPU everywhere) | Metal via WebGPU, browser WebGPU | Large native builds (Dawn) or a Rust toolchain (wgpu); WebGPU only in the browser, no WebGL2 fallback |

### B: A native Metal renderer plus a separate web renderer (e.g. three.js)

- Each side uses its most natural tools, and three.js is quick to start with.
- Camera, picking, highlighting, edge rendering and every later feature exist twice and drift
  apart. Swift and TypeScript code cannot share tests.

## Decision

**Option A with sokol_gfx.** The viewport renderer is a separate C++ library, `facetry-render`
(`render/`):

- It does **not** depend on OpenCASCADE. It takes the mesh buffers the core produces (same
  layout as `facetry_mesh_buffer_kind`) and has its own small C API, `render/include/facetry_render.h`,
  following the same rules as ADR-0003 (opaque handle, status codes, no exceptions across it).
- **Backends:** Metal on Apple platforms (the app passes its `MTLDevice` and `MTKView` drawable),
  and WebGL2 in the browser. A WebGPU backend can be added later through sokol without
  changing the renderer.
- **Shaders** are written once in annotated GLSL (`render/shaders/viewport.glsl`) and compiled
  by a pinned `sokol-shdc` into a generated header that is checked in.
- **Picking runs on the CPU, not through a GPU ID buffer** (this changes the original M0.4 plan):
  - Faces are picked by a ray cast against a BVH of the triangles.
  - Edges and vertices are picked by screen-space distance within a configurable radius (larger
    for fingers, smaller for the Pencil and mouse), and checked for occlusion by a ray cast.
  - This needs no GPU readback (sokol_gfx deliberately has none, and readbacks stall the GPU),
    behaves identically on every backend, and is unit-tested like the rest of the core.
    OpenCASCADE's own viewer also selects on the CPU with a BVH.
- **Camera math is shared C++ as well:**
  - Z-up turntable orbit around the point under the cursor, pan, and zoom to the cursor.
  - Perspective and orthographic projection, with the clip-space depth range chosen per backend.
- **Highlights** are uniforms (hovered and selected face, edge and vertex ID) compared against
  per-vertex IDs in the shaders, so hovering never touches the vertex buffers.
- **Threads:**
  - On the web, the renderer is its own small WebAssembly module on the UI thread. The geometry
    core stays in its Web Worker and sends mesh buffers over.
  - On Apple platforms, the renderer runs in the `MTKView` draw callback, and the core is called
    off the main thread.

## Consequences

- Rendering features are written once. Camera, picking and highlighting have unit tests that run
  on macOS, in the iOS simulator and in Node.
- sokol_gfx is pinned to a commit and fetched with a hash, like the other header-only
  dependencies. `sokol-shdc` is pinned the same way, and a CI step checks that the generated
  shader header is up to date.
- The Apple build compiles one Objective-C++ file (the sokol Metal backend) with ARC.
- CPU picking must stay fast for large models: the BVH is built once per mesh, and screen-space
  edge picking is linear in edge segments (well below a millisecond at M0.4 sizes). If that
  stops being enough, edge segments go into a screen-space grid.
- The web app loads two WebAssembly modules: the large core in the worker and the small renderer
  on the UI thread.
