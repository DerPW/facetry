# Building Facetry

Facetry has three parts that build independently:

| Part | Location | Build system |
|---|---|---|
| OpenCASCADE (prebuilt) | `third_party/occt/` | Download script, or build scripts for new versions |
| Core (C++20) | `core/` | CMake presets (`CMakePresets.json`) |
| Apple app (iPadOS + macOS) | `apps/apple/` | XcodeGen → Xcode |
| Web app | `apps/web/` | npm + Vite |

## Prerequisites

| Tool | Needed for | Install |
|---|---|---|
| Xcode 26 or newer | Core (Apple), Apple app | App Store |
| CMake 3.28+ and Ninja | Core | `brew install cmake ninja` |
| Emscripten 6.0.12 | Core (WebAssembly) | [emsdk](https://emscripten.org/docs/getting_started/downloads.html), see below |
| XcodeGen | Apple app | `brew install xcodegen` |
| Node.js 24 LTS | Web app | [nodejs.org](https://nodejs.org/) or `brew install node@24` |
| clang-format 23.1.3 | C++ formatting | `pipx install clang-format==23.1.3` |

## OpenCASCADE

The core links a prebuilt OpenCASCADE artifact. Download it once (and again after `occt.env`
changes):

```bash
third_party/occt/fetch.sh apple   # for the macos-* and ios-* presets
third_party/occt/fetch.sh wasm    # for the wasm-* presets
```

Building OpenCASCADE from source is only needed for a new version or new build options. See
[third_party/occt/README.md](../third_party/occt/README.md).

## Core

Presets for every target:

| Preset | Target | Tests |
|---|---|---|
| `macos-debug`, `macos-release` | macOS (Apple silicon) | ✅ native |
| `ios-simulator` | iOS simulator (arm64) | ✅ in the simulator |
| `ios-device` | iOS device (arm64) | build only |
| `wasm-debug`, `wasm-release` | WebAssembly | ✅ in Node |

```bash
cmake --preset macos-debug
cmake --build --preset macos-debug
ctest --preset macos-debug
```

`ctest --preset ios-simulator` runs the test binaries inside the iOS simulator with
`xcrun simctl spawn`. It uses a booted simulator, or boots the first available iPad
(`cmake/ios-simulator-run.sh`; set `FACETRY_SIMULATOR_UDID` to choose one).

Build output goes to `build/<preset>/`. CI configures with `-DFACETRY_WARNINGS_AS_ERRORS=ON`.
Use the same option locally to catch warnings before pushing.

### WebAssembly

Install and activate the pinned Emscripten version once:

```bash
git clone --branch 6.0.12 https://github.com/emscripten-core/emsdk.git ~/emsdk
~/emsdk/emsdk install 6.0.12 && ~/emsdk/emsdk activate 6.0.12
```

Then, in every new shell, load its environment before configuring:

```bash
source ~/emsdk/emsdk_env.sh
cmake --preset wasm-debug && cmake --build --preset wasm-debug && ctest --preset wasm-debug
```

The `wasm-*` builds also produce the web app's two modules and copy them into the app: the
core (`apps/web/src/core/wasm/`) and the renderer (`apps/web/src/render/wasm/`). Use the release
preset for the web app:

```bash
cmake --preset wasm-release && cmake --build --preset wasm-release --target facetry-core-web facetry-render-web
```

The `wasm-*` presets find the toolchain through the `EMSDK` environment variable. The modules
are built for the oldest browsers in
[ADR-0009](adr/0009-minimum-os-and-browser-versions.md) (`MIN_*_VERSION` in `CMakeLists.txt`).

### Command-line tool

`facetry-cli` builds with the macOS and WebAssembly presets. The WebAssembly version runs in Node
with access to local files:

```bash
build/macos-release/tools/cli/facetry-cli check tests/models
build/macos-release/tools/cli/facetry-cli bench tests/documents/corner-fillet.json
node build/wasm-release/tools/cli/facetry-cli.js bench tests/documents/corner-fillet.json
```

`check` compares the golden models with their expected values (also part of `ctest`), `info`
prints a model's properties and can write an STL, and `bench` times the regeneration of a
document ([benchmarks](benchmarks.md#regeneration)).

## Viewport renderer

The renderer (`render/`, [ADR-0004](adr/0004-viewport-renderer-shared-cpp-sokol.md)) builds with
the same presets as the core; its tests (`facetry-render-tests`) run with `ctest`. Its shaders
are written in `render/shaders/viewport.glsl` and compiled with a pinned sokol-shdc into
`render/src/shaders/viewport.glsl.h`, which is checked in. After changing the shaders:

```bash
render/shaders/generate.sh
```

CI fails if the checked-in header does not match the shader source.

## Apple app

The app links the core as `CFacetryCore.xcframework` (static library plus the C API as a Clang
module) and embeds `OCCT.xcframework`. Both are built into `apps/apple/Frameworks/` (not checked
in) by a script; run it again after changing core code:

```bash
apps/apple/scripts/build-core.sh
```

The Xcode project is generated from `apps/apple/project.yml` and is not checked in. Generate it
again after pulling changes that touch `project.yml` or add files:

```bash
cd apps/apple && xcodegen generate && open Facetry.xcodeproj
```

The app is document-based: it opens and saves `.facetry` documents (type
`io.github.derpw.facetry.document`, declared in `project.yml`). Until the file format is decided
(ADR-0008, M0.7) a document is a small JSON file with a format version.

The app target supports iPad (iPadOS 26+) and native macOS (macOS 26+), Apple silicon only. To
build and test without a signing identity, as CI does:

```bash
xcodebuild -project apps/apple/Facetry.xcodeproj -scheme Facetry -destination 'generic/platform=iOS Simulator' CODE_SIGNING_ALLOWED=NO build
```

```bash
xcodebuild -project apps/apple/Facetry.xcodeproj -scheme Facetry -destination 'platform=macOS,arch=arm64' CODE_SIGN_IDENTITY=- CODE_SIGN_STYLE=Manual DEVELOPMENT_TEAM= test
```

## Web app

The web app needs the core module first (see [WebAssembly](#webassembly)). Then:

```bash
cd apps/web && npm install && npm run dev
```

The core runs in a Web Worker (`src/core/core.worker.ts`). The UI thread talks to it through
`CoreClient` (`src/core/client.ts`) and renders with the viewport (`src/render/viewport.ts`).

Development URLs:

| URL | Purpose |
|---|---|
| `/?stress` | Tessellates the demo part finely (about 215k triangles) |
| `/?stress&bench` | Also measures draw and pick times and prints them in the dev server's log (`[bench]`). Useful in browsers that cannot be scripted, such as Safari |

In development builds the browser console has `window.facetry.viewport` (e.g.
`facetry.viewport.benchmark(60)`) and `window.facetry.core`.

The Apple app takes `--stress` as a launch argument for the same fine tessellation.

The UI is React ([ADR-0007](adr/0007-web-ui-framework-react.md)). Production builds include a
generated service worker (`vite/service-worker.ts`) that precaches the whole app for offline use;
development builds never register it. CI deploys every build of `main` to GitHub Pages:
GitHub Pages.

| Script | Purpose |
|---|---|
| `npm run dev` | Development server |
| `npm run build` | Type-check and production build into `dist/` |
| `npm test` | Vitest: the TypeScript core API against the real WebAssembly module, in Node |
| `npm run lint` | ESLint |
| `npm run format` / `format:check` | Prettier |

## App icon

The icon is drawn in code, so it stays reproducible. After changing the design, run on macOS:

```bash
swift tools/icons/render-icons.swift
```

It writes the web icons (`apps/web/public/icons/`) and the Apple app icon set
(`apps/apple/Facetry/Assets.xcassets/`), which are checked in.

## Formatting

| Language | Tool | Check | Fix |
|---|---|---|---|
| C++ | clang-format 23.1.3 (`.clang-format`) | `git ls-files '*.cpp' '*.hpp' \| xargs clang-format --dry-run --Werror` | same with `-i` |
| Swift | swift-format, bundled with Xcode (`.swift-format`) | `swift format lint --strict --recursive apps/apple` | `swift format --in-place --recursive apps/apple` |
| TypeScript, CSS, HTML | Prettier (`apps/web/.prettierrc.json`) | `npm run format:check` | `npm run format` |

## Continuous integration

`.github/workflows/ci.yml` runs on every pull request and on `main`:

| Check | Runner | What it does |
|---|---|---|
| `OpenCASCADE (Apple)`, `OpenCASCADE (WebAssembly)` | macOS 26, Ubuntu 24.04 | Provide the OCCT artifact: from the cache, from the release, or built from source when a new revision has no release yet |
| `core (macOS)` | macOS 26 | Build and test the core natively, check the golden models with `facetry-cli` |
| `core (iOS)` | macOS 26 | Build the core for the iOS simulator and device; run the tests in the simulator |
| `core (WebAssembly)` | Ubuntu 24.04 | Build the core with Emscripten, run its tests and the golden models in Node, build the web module |
| `Apple app` | macOS 26 | Build the core frameworks and the app, run the Swift tests on macOS, lint Swift |
| `web app` | Ubuntu 24.04 | Lint, format check, test against the core module, type-check and build |
| `Deploy web app to GitHub Pages` | Ubuntu 24.04 | `main` only: publishes the web build |
| `format (C++)` | Ubuntu 24.04 | clang-format check; checks that the generated shader header is up to date |

All checks above are required to merge into `main`. A seventh job, `Publish OpenCASCADE
artifacts`, runs only after CI built a new OpenCASCADE revision from source. It uploads the new
artifacts to the `occt-<version>-r<rev>` release.
