# ADR-0002: Shared C++ core with native and web shells

- **Status:** Accepted
- **Date:** 2026-10-16
- **Milestone:** M0.1

## Context

Facetry targets three platforms:

- **iPadOS:** the primary touch and Apple Pencil experience, distributed through the App Store
- **macOS:** desktop use with keyboard, mouse and trackpad, distributed as a direct download
- **Web:** for trying and sharing without an install, hosted on GitHub Pages

The Apple Pencil experience is the main differentiator. It needs full access to Pencil
features (hover, double-tap, squeeze, barrel roll, low latency), which only native code has.
At the same time, the project is developed by one maintainer with AI assistance, so duplicated
work has to be kept small.

## Options

### Web app everywhere (wrapped in WKWebView on Apple)

- One UI code base, fastest iteration.
- Pencil Pro features are only available through custom native bridges, and input latency is
  higher. WebAssembly memory limits apply on iPad too. The result feels less native, and App
  Store review is less predictable.

### Native apps only

- Best experience on Apple platforms.
- No web version for sharing. That was an explicit goal.

### Separate cores per platform

- No cross-compilation work.
- Two implementations of the hardest part (modeling, regeneration, naming). Documents would
  drift apart between platforms.

### Shared C++ core, thin native and web shells

- One implementation of everything that is not platform UI: geometry, document model,
  regeneration, undo, sketch solving, tool logic, snapping, selection logic, import/export.
- Native SwiftUI + Metal on iPadOS/macOS, TypeScript on the web with the core as WebAssembly.
- Two UI shells to maintain, but each one is thin.

### Rust core

- Good WebAssembly tooling and memory safety.
- The kernel (ADR-0001) and the sketch solver are C++. Bindings would add a layer without
  removing the C++ build.

## Decision

Build **one shared C++20 core** (`facetry-core`) that compiles natively for Apple platforms and
to WebAssembly. Build **two thin shells** on top: SwiftUI for iPadOS and macOS (one
multiplatform target), and TypeScript for the web.

## Consequences

- Logic that should behave the same on all platforms goes in the core, including tool state
  machines and snapping. The shells render panels and forward input events (see the
  [architecture rules](../ARCHITECTURE.md#rules)).
- The API boundary between core and shells is decided in ADR-0003 (M0.3). The renderer is
  decided in ADR-0004 (M0.4).
- CI builds and tests the core for macOS, iOS and WebAssembly on every pull request, and builds
  both shells.
- Platform differences are limited to platform capabilities and documented in
  [Platforms](../PLATFORMS.md).
- The Apple project is described in XcodeGen's `apps/apple/project.yml`. The generated Xcode
  project is not checked in, which keeps diffs reviewable.
