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

## Development and test only

These components are used to build or test Facetry. They are not part of the shipped app.

| Component | Version | License | Used for |
|---|---|---|---|
| [doctest](https://github.com/doctest/doctest) | 2.5.3 | MIT | C++ unit tests |
| [Vite](https://vite.dev/) | 8.x | MIT | Web app build |
| [TypeScript](https://www.typescriptlang.org/) | 6.0.x | Apache-2.0 | Web app type checking |
| [ESLint](https://eslint.org/), [typescript-eslint](https://typescript-eslint.io/) | 10.x, 8.x | MIT | Web app linting |
| [Prettier](https://prettier.io/) | 3.x | MIT | Web app formatting |
| [XcodeGen](https://github.com/yonaskolb/XcodeGen) | 2.45+ | MIT | Generating the Xcode project |
