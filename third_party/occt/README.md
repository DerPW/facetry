# OpenCASCADE

Facetry's geometry kernel ([ADR-0001](../../docs/adr/0001-geometry-kernel-opencascade.md)).
OpenCASCADE is large and takes a long time to build, so it is built once per version and
published as a prebuilt artifact on this repository's
[GitHub releases](https://github.com/DerPW/facetry/releases) (tags `occt-<version>-r<rev>`).

| File | Purpose |
|---|---|
| `occt.env` | Version pin, source checksum, build revision, toolkit list, Apple deployment target |
| `fetch.sh` | Downloads and installs a prebuilt artifact (the normal way) |
| `build.sh` | Builds the static libraries for one target from source |
| `package.sh` | Turns the builds into the artifact (Apple framework or WebAssembly libraries) |
| `common.sh` | Shared paths and helpers |

## Using the prebuilt artifacts

```bash
third_party/occt/fetch.sh apple   # macOS, iOS device, iOS simulator
third_party/occt/fetch.sh wasm    # WebAssembly
```

Both install into `third_party/_install/occt-<version>-r<rev>/`, where
[`cmake/FacetryOCCT.cmake`](../../cmake/FacetryOCCT.cmake) picks them up as the target
`facetry::occt`.

## What is in an artifact

| Artifact | Contents | Linking |
|---|---|---|
| `…-apple.tar.gz` | `OCCT.xcframework` with slices `macos-arm64`, `ios-arm64`, `ios-arm64-simulator`; headers; licenses | Dynamic: all toolkits in one `OCCT.framework`, so the LGPL component can be replaced |
| `…-wasm.tar.gz` | Static libraries (`libTK*.a`); headers; licenses | Static, built with `-fwasm-exceptions -msimd128` |

Each artifact has a `BUILDINFO.txt` with the exact version, toolkits and build machine.

**Toolkits:** modeling (primitives, booleans, fillets, offsets, local features), meshing, shape
healing, STEP and STL. STEP needs TKXCAF, which pulls in TKService and TKV3d. They are built
without OpenGL and FreeType. No Draw harness, no OCAF beyond what STEP needs.

## Building from source

Only needed when changing `occt.env` or the build options, or to work offline.

```bash
third_party/occt/build.sh macos
third_party/occt/build.sh ios-simulator
third_party/occt/build.sh ios
third_party/occt/package.sh apple

source ~/emsdk/emsdk_env.sh        # see docs/BUILDING.md
third_party/occt/build.sh wasm
third_party/occt/package.sh wasm
```

Builds use OCCT's precompiled headers and all CPU cores. One target takes 10–15 minutes on an
M3 MacBook and noticeably longer on CI runners.

## Changing the build

1. Change `occt.env`: a new `OCCT_VERSION` (with `OCCT_TAG` and `OCCT_SHA256`), or the same
   version with the next `OCCT_BUILD_REV` when only options or toolkits change.
2. Open a pull request. CI finds no release for the new revision, builds OpenCASCADE from
   source (slow, once), runs all core tests against it and then publishes the artifacts to a
   new `occt-<version>-r<rev>` release.
3. Release assets are never replaced. A different build always gets a new revision.

## License

OpenCASCADE Technology is licensed under the
[GNU LGPL 2.1 with the OCCT exception](https://dev.opencascade.org/resources/licensing). The
license texts are part of every artifact (`licenses/`) and are listed in
[THIRD_PARTY_NOTICES.md](../../THIRD_PARTY_NOTICES.md).
