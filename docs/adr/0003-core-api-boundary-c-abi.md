# ADR-0003: Core API boundary: a C ABI with handles

- **Status:** Accepted
- **Date:** 2026-10-16
- **Milestone:** M0.3

## Context

The shared C++ core ([ADR-0002](0002-shared-core-with-native-and-web-shells.md)) is used by two
shells: Swift on iPadOS/macOS, and TypeScript in a Web Worker on top of WebAssembly. The
boundary between core and shells must:

- work the same way for Swift and TypeScript, so both shells can be tested against one contract;
- never let C++ or OpenCASCADE exceptions cross it;
- keep OpenCASCADE types inside the kernel adapter;
- let shells read large buffers (meshes) without extra copies inside the core;
- stay small for the WebAssembly download.

[Prior art](../PRIOR-ART.md) shows two ways to build such a boundary:

- **opencascade.js** binds almost all of OpenCASCADE through Embind. The bindings are generated
  and very large, rely on JavaScript exceptions, and are hard to maintain.
- **occt-wasm** exposes a small facade with integer handles and an error-code enum. It is small
  and robust.

## Options

### A: Plain C ABI with opaque handles

One C header for both shells. Objects are referred to by `uint32_t` handles inside a context.
Every function returns a status code, and the context keeps the last error message.

- Swift imports C headers natively (Clang module). No C++ interop settings are needed.
- WebAssembly exports the functions directly, and TypeScript calls them through a thin wrapper.
  No Embind, so the code stays smaller.
- Both shells need a small hand-written wrapper.

### B: Swift C++ interop on Apple, Embind on the web

- Richer types on both sides.
- Two different boundaries with different semantics.
- Swift C++ interop has to be enabled for the whole target. Embind adds code size and runtime
  type registration.
- Exceptions still have to be stopped at two separate places.

### C: A message protocol (commands as JSON or binary messages)

- Very decoupled, and already shaped like the worker messages the web needs.
- Every call is serialized, including inside the native apps, and mesh buffers need a separate
  path.
- Better suited as a layer on top of the core API, which is how the web worker uses it.

## Decision

**Option A.** The core exposes one C header, [`core/include/facetry_core.h`](../../core/include/facetry_core.h):

- **Context:** a `facetry_context` owns all objects and the last error.
- **Handles:** objects (shapes, meshes) are `uint32_t` handles. `0` is never a valid handle.
  Handles are released explicitly, or all at once when the context is destroyed.
- **Errors:** every function returns `facetry_status`. On failure, `facetry_last_error_message`
  returns a human-readable message for that context. All exceptions are caught inside the core.
- **Arguments:** scalar arguments only, plus caller-provided output pointers. The single struct,
  `facetry_shape_properties`, contains only `double` fields, so its layout is trivial to read
  from TypeScript.
- **Buffers:** `facetry_mesh_buffer` returns a pointer and element count for one mesh buffer.
  The data stays valid until the mesh is released. Shells copy it once, into GPU buffers or
  transferable `ArrayBuffer`s.
- **Versioning:** `FACETRY_API_VERSION_MAJOR/MINOR` in the header and `facetry_api_version()` at
  runtime. A shell refuses to run against a core with a different major version.
- **Logging:** one process-wide callback with levels. Swift forwards to OSLog, the web worker
  forwards to the console.

The idiomatic APIs live in the shells: `FacetryCore` in Swift, and the `FacetryCore` class plus
a postMessage RPC client in TypeScript.

## Consequences

- The C++ code behind the header (`facetry::core`) is free to change. Only `facetry_core.h` is a
  contract, versioned as described above.
- Adding a feature means adding C functions plus the wrapper code in Swift and TypeScript. When
  the API grows, generating the wrappers from the header becomes worthwhile.
- On WebAssembly the C functions are marked `EMSCRIPTEN_KEEPALIVE`, and the core library is
  linked as a whole archive into the web module. No export list has to be maintained.
- Kernel failures become `FACETRY_ERROR_KERNEL` with OpenCASCADE's message. A test calls a
  failing operation many times in a row to make sure repeated caught exceptions leave the
  WebAssembly stack intact (a problem occt-wasm ran into).
- Topology IDs (face, edge and vertex numbers) are only valid for one shape object. Persistent
  references that survive edits come with M0.6
  ([ADR-0006](../ARCHITECTURE.md#decisions)).
