# Facetry — notes for AI agents

Facetry is a parametric 3D CAD app (Fusion 360 workflow, pencil-first UI) for iPadOS, macOS and
the web, built on one shared C++ core over OpenCASCADE.

Start here: [docs/ROADMAP.md](docs/ROADMAP.md), [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md),
[docs/PLATFORMS.md](docs/PLATFORMS.md), [docs/BUILDING.md](docs/BUILDING.md).
Before designing a subsystem, check [docs/PRIOR-ART.md](docs/PRIOR-ART.md) for how other
projects solved it.

## Verify before pushing

Run the checks for the parts you touched (CI runs all of them and they are required):

- OpenCASCADE: `third_party/occt/fetch.sh apple` (and `wasm`) once, before configuring the core
- Core: `cmake --preset macos-debug -DFACETRY_WARNINGS_AS_ERRORS=ON && cmake --build --preset macos-debug && ctest --preset macos-debug`
- Core in the iOS simulator: `cmake --preset ios-simulator && cmake --build --preset ios-simulator && ctest --preset ios-simulator`
- C++ formatting: clang-format 23.1.3 `--dry-run --Werror` on changed `.cpp`/`.hpp` files
- Apple app: `apps/apple/scripts/build-core.sh`, `cd apps/apple && xcodegen generate`, then
  `xcodebuild … build` for the iPadOS simulator, `xcodebuild … test` on macOS (see
  docs/BUILDING.md), and `swift format lint --strict --recursive apps/apple`
- Web app: build the web modules (`cmake --build --preset wasm-release --target
  facetry-core-web facetry-render-web`), then `cd apps/web && npm run lint && npm run format:check && npm test && npm run build`
- Golden models: `build/macos-debug/tools/cli/facetry-cli check tests/models`
- Shaders: after editing `render/shaders/viewport.glsl`, run `render/shaders/generate.sh`
- Viewport changes: check the web app in a browser (`npm run dev`, `/?stress&bench` for numbers)
  and the Mac app (Metal) before pushing; rendering has no automated image tests yet
- C API changes: update `facetry_core.h`, both shell wrappers (Swift `Core.swift`, TypeScript
  `api.ts` + `wasm/facetry-core.d.mts`) and bump `FACETRY_API_VERSION_*` as described in ADR-0003

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
- Input handling follows `docs/INPUT.md` on every platform; change the table and both shells together.
