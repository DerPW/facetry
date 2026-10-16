# Architecture Decision Records

An ADR records one significant technical decision: the context, the options, what was decided
and what follows from it. ADRs explain *why* the code looks the way it does, which is especially
useful in a project where much of the code is written with AI assistance.

## How to write one

1. Copy [`template.md`](template.md) to `NNNN-short-title.md`, using the next free number.
2. Fill it in. Keep it short; link to issues, benchmarks or spikes for details.
3. Open a pull request. The ADR starts as **Proposed** and becomes **Accepted** when the PR is
   merged.
4. Add it to the index below and to the decision table in
   [Architecture](../ARCHITECTURE.md#decisions).

An accepted ADR is not edited later except for small clarifications. To change a decision, write
a new ADR that supersedes the old one and set the old one's status to
**Superseded by ADR-NNNN**.

## Index

| ADR | Title | Status |
|---|---|---|
| [0001](0001-geometry-kernel-opencascade.md) | Geometry kernel: OpenCASCADE | Accepted |
| [0002](0002-shared-core-with-native-and-web-shells.md) | Shared C++ core with native and web shells | Accepted |
| [0003](0003-core-api-boundary-c-abi.md) | Core API boundary: a C ABI with handles | Accepted |
| [0004](0004-viewport-renderer-shared-cpp-sokol.md) | Viewport renderer: shared C++ on sokol_gfx, CPU picking | Accepted |
| [0007](0007-web-ui-framework-react.md) | Web UI framework: React | Accepted |
