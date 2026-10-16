# Facetry — notes for AI agents

Facetry is a parametric 3D CAD app (Fusion 360 workflow, pencil-first UI) for iPadOS, macOS and
the web, built on one shared C++ core over OpenCASCADE.

Start here: [docs/ROADMAP.md](docs/ROADMAP.md), [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md),
[docs/PLATFORMS.md](docs/PLATFORMS.md).

## Rules

- **English only** in code, comments, docs, commits, issues and PRs. The maintainer may talk to
  you in German; the repository stays English.
- **Never commit to `main`.** It is protected. Work on a branch (`feat/…`, `fix/…`, `docs/…`, …)
  and open a pull request. PRs are squash-merged, so the PR title must be a Conventional Commit.
- **Scope each PR to one roadmap item or issue.** When a roadmap item is done, tick its checkbox
  in `docs/roadmap/phase-*.md` in the same PR.
- **Follow the architecture rules** in `docs/ARCHITECTURE.md#rules`: logic goes in the core,
  OCCT types stay inside the kernel adapter, the core never blocks the UI, no exceptions cross
  the API boundary.
- **Record decisions as ADRs** in `docs/adr/` and update the decision table in
  `docs/ARCHITECTURE.md`.
- **Core changes need tests.** Modeling changes need golden-model or edit tests in
  `tests/models/`.
- Ideas that fit no milestone go to `docs/roadmap/future.md` instead of into the current work.
