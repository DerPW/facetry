# ADR-0009: Minimum OS and browser versions

- **Status:** Accepted
- **Date:** 2026-10-16
- **Milestone:** M0.7

## Context

Supporting older systems reaches more users. Requiring newer ones gives better APIs (SwiftUI,
Pencil, WebAssembly features) and less testing. Where things stand in October 2026:

- **Apple:**
  - iPadOS 27 and macOS 27 are current, released in September 2026.
  - Since M0.2, OpenCASCADE and the core are built with a deployment target of **26.0**
    (`third_party/occt/occt.env`, `CMakePresets.json`), and the apps use the same.
- **Web:** the WebAssembly modules use:
  - WebAssembly SIMD (Chrome 91, Firefox 89, Safari 16.4);
  - WebAssembly exception handling in its first ("legacy") form, Emscripten's default (Chrome 95,
    Firefox 100, Safari 15.2);
  - module workers for the core (Firefox 114, Safari 15);
  - WebGL2 for the viewport.

## Options

### Only the current major versions

- Least testing and newest APIs.
- Excludes iPads and Macs that have not updated yet, often for months after a release.

### Current major − 1 on Apple, a feature-based floor on the web

- One year of OS releases on Apple. On the web, everything that has the required features.
- Two Apple OS versions to keep working; the web floor follows what the build really needs.

### Older Apple versions (current − 2 or more)

- More devices, but the deployment target is baked into the OCCT build. Older SwiftUI and Pencil
  APIs would need fallbacks everywhere, for a small group of users.

## Decision

**Apple: iPadOS 26 and macOS 26 or newer (current major − 1), Apple silicon only on the Mac.**

- iPadOS 26 runs on every iPad from 2018 on: A12 chip or newer, i.e. iPad Pro (2018), iPad
  Air (3rd generation), iPad (8th generation) and iPad mini (5th generation) and later
  ([list](https://everymac.com/systems/apple/ipad/ipad-faq/ipados-26-supported-devices-ipad-system-requirements.html)).
- Pencil features depend on the hardware, not the OS version, and are detected at runtime:
  - hover needs an M2 iPad Pro or newer;
  - squeeze and barrel roll need Apple Pencil Pro.
- The minimum moves up once a year, after a new major release has been out for a few months, in
  one PR that updates `occt.env` (and rebuilds OCCT), the CMake presets and
  `apps/apple/project.yml`.

**Web: supported are the current and previous major versions of Chrome, Edge, Firefox and Safari,
on desktop and iPadOS.** The build targets a lower floor where it still loads:

| Browser | Floor | Set by |
|---|---|---|
| Chrome, Edge | 111 | Vite's "baseline widely available" target |
| Firefox | 114 | module workers |
| Safari (macOS, iPadOS) | 16.4 | WebAssembly SIMD |

- The floor is written into the build:
  - Emscripten's `MIN_CHROME_VERSION`, `MIN_FIREFOX_VERSION` and `MIN_SAFARI_VERSION` in
    `CMakeLists.txt`;
  - Vite's `build.target` in `apps/web/vite.config.ts`.
- Between the floor and the supported versions, the app should work but is not tested.
- **Not used yet:**
  - the standardized WebAssembly exceptions (`exnref`: Chrome 137, Firefox 131, Safari 18.4);
  - threads, which need cross-origin isolation (M2.9).

  Adopting either raises the floor and updates this ADR.

**Tooling:** Node 24 LTS for the web build and for running the WebAssembly tests.

## Consequences

- **CI:**
  - It runs the native tests on the newest macOS runner and in an iOS ≥ 26 simulator
    (`cmake/ios-simulator-run.sh` already picks one).
  - Browsers are checked by hand (Chromium, Safari, Firefox) until browser tests are automated.
- **Older browsers:** below the floor the core module fails to load. The web app should detect
  the missing features at startup and say so instead of showing an empty page (M1.10).
- **Platform table:** [Platforms](../PLATFORMS.md) lists these versions.
