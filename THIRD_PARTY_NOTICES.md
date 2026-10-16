# Third-party notices

Facetry uses the following third-party components. This file is shipped with every release and
shown in the app's About screen.

## Shipped with the app

### OpenCASCADE Technology 8.0.1

- Website: <https://dev.opencascade.org/>
- Source: <https://github.com/Open-Cascade-SAS/OCCT> (tag `V8_0_1`), unmodified
- License: GNU Lesser General Public License version 2.1, with the Open CASCADE Exception 1.0
- Copyright © Open CASCADE SAS

Facetry links OpenCASCADE dynamically on Apple platforms (`OCCT.framework`) and statically in
the WebAssembly build. All of Facetry is open source, and the build scripts in
[`third_party/occt/`](third_party/occt/README.md) let you rebuild it against a modified
OpenCASCADE. The full license texts ship with every OpenCASCADE artifact (`licenses/`) and in
the app.

### sokol_gfx

- Source: <https://github.com/floooh/sokol> (`sokol_gfx.h`, `sokol_log.h`, commit `5e94d1291fea`)
- License: zlib/libpng
- Copyright © Andre Weissflog

Used by the viewport renderer (`render/`) on all platforms.

### React and React DOM 19

- Source: <https://github.com/facebook/react>
- License: MIT
- Copyright © Meta Platforms, Inc. and affiliates

Used by the web app's UI.

## Development and test only

These components are used to build or test Facetry. They are not part of the shipped app.

| Component | Version | License | Used for |
|---|---|---|---|
| [doctest](https://github.com/doctest/doctest) | 2.5.3 | MIT | C++ unit tests |
| [nlohmann/json](https://github.com/nlohmann/json) | 3.12.0 | MIT | JSON model descriptions in `facetry-cli` and the golden tests (not linked into the apps) |
| [Vitest](https://vitest.dev/) | 5.x | MIT | Web app tests |
| [@vitejs/plugin-react](https://github.com/vitejs/vite-plugin-react), [eslint-plugin-react-hooks](https://github.com/facebook/react) | 6.x, 7.x | MIT | Web app build and linting |
| [sokol-shdc](https://github.com/floooh/sokol-tools) | `b975d5642093` | MIT | Compiling the viewport shaders (the generated header is checked in) |
| [Vite](https://vite.dev/) | 8.x | MIT | Web app build |
| [TypeScript](https://www.typescriptlang.org/) | 6.0.x | Apache-2.0 | Web app type checking |
| [ESLint](https://eslint.org/), [typescript-eslint](https://typescript-eslint.io/) | 10.x, 8.x | MIT | Web app linting |
| [Prettier](https://prettier.io/) | 3.x | MIT | Web app formatting |
| [XcodeGen](https://github.com/yonaskolb/XcodeGen) | 2.45+ | MIT | Generating the Xcode project |
