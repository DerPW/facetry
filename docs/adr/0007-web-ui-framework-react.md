# ADR-0007: Web UI framework: React

- **Status:** Accepted
- **Date:** 2026-10-16
- **Milestone:** M0.5

## Context

The web shell needs panels and controls around the viewport. In the next phases these become
the browser tree, the timeline, property panels, the context toolbar, the command search, the
marking menu, dialogs and later the blocks editor (M3.6). Following
[ADR-0002](0002-shared-core-with-native-and-web-shells.md), the shell stays thin: modeling,
picking and rendering live in C++ (the core in a Web Worker, the renderer on the UI thread). The
UI framework therefore only renders panels and forwards input.

Until now the web app built its few DOM elements by hand. That does not scale to the UI above.

## Options

### React

- The largest ecosystem for exactly the widgets needed: accessible menus and dialogs (Radix UI),
  command palettes (cmdk), drag and drop for trees and the timeline (dnd-kit), and virtualized
  lists for large models.
- Declarative components, close to how the Swift shell is written in SwiftUI.
- By far the most common framework in AI coding assistants' training data, which matters for a
  project built largely with AI (see the [README](../../README.md#built-with-ai)).
- A larger runtime than the alternatives (about 60 KB gzip). That is small next to the 2.6 MB core
  module.

### Svelte 5

- Compiled, small, concise components.
- Fewer ready-made widgets, and smaller AI training coverage.

### Preact or SolidJS

- Small and fast. Preact is mostly React-compatible, but compatibility gaps show up exactly in
  the widget libraries that are the reason to choose React.

### Keep vanilla TypeScript

- No dependency.
- Hand-written state handling and DOM updates for a growing UI.

## Decision

**React 19** with TypeScript, built by Vite with `@vitejs/plugin-react`, and linted with the React
Hooks rules.

- The viewport is a React component that owns the canvas and the `Viewport` object (WebGL2
  renderer). Rendering and input stay outside React's render cycle.
- Shared UI state is plain React state and context for now. A state library is only added when a
  concrete need appears.
- Widget libraries are added when the features that need them arrive, each with its license
  recorded in `THIRD_PARTY_NOTICES.md`.

## Consequences

- The web shell can grow the same component structure as the SwiftUI shell, which keeps the two
  UIs comparable.
- High-frequency updates (frame statistics, hover) must not re-render large parts of the tree.
  They are either throttled (stats once per second) or kept out of React state (hover lives in
  the renderer).
