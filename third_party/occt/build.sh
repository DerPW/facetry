#!/usr/bin/env bash
# Builds static OpenCASCADE libraries for one target.
#
#   third_party/occt/build.sh macos|ios|ios-simulator|wasm
#
# Output: third_party/_build/occt/<target>/install (headers in include/opencascade, libs in lib).
# The wasm target needs an activated emsdk (EMSDK set). Package the results with package.sh.
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

target="${1:-}"
case "$target" in
    macos | ios | ios-simulator | wasm) ;;
    *) die "usage: build.sh macos|ios|ios-simulator|wasm" ;;
esac

fetch_source() {
    local tarball="$OCCT_WORK_DIR/src/occt-${OCCT_VERSION}.tar.gz"
    if [[ -f "$OCCT_SRC_DIR/CMakeLists.txt" ]]; then
        return
    fi
    mkdir -p "$OCCT_WORK_DIR/src"
    if [[ ! -f "$tarball" ]]; then
        log "Downloading OpenCASCADE $OCCT_VERSION ($OCCT_TAG)"
        curl -fsSL -o "$tarball.part" \
            "https://github.com/Open-Cascade-SAS/OCCT/archive/refs/tags/${OCCT_TAG}.tar.gz"
        mv "$tarball.part" "$tarball"
    fi
    local actual
    actual="$(sha256_of "$tarball")"
    [[ "$actual" == "$OCCT_SHA256" ]] || die "SHA256 mismatch for $tarball: $actual"
    log "Extracting source"
    rm -rf "$OCCT_SRC_DIR"
    mkdir -p "$OCCT_SRC_DIR"
    tar -xzf "$tarball" -C "$OCCT_SRC_DIR" --strip-components=1
}

fetch_source

build_dir="$OCCT_WORK_DIR/$target/build"
prefix="$OCCT_WORK_DIR/$target/install"
rm -rf "$prefix"

args=(
    -S "$OCCT_SRC_DIR" -B "$build_dir" -G Ninja
    -D CMAKE_BUILD_TYPE=Release
    -D BUILD_LIBRARY_TYPE=Static
    -D INSTALL_DIR="$prefix"
    -D INSTALL_DIR_INCLUDE=include/opencascade
    -D INSTALL_DIR_LIB=lib
    -D BUILD_MODULE_FoundationClasses=OFF
    -D BUILD_MODULE_ModelingData=OFF
    -D BUILD_MODULE_ModelingAlgorithms=OFF
    -D BUILD_MODULE_Visualization=OFF
    -D BUILD_MODULE_ApplicationFramework=OFF
    -D BUILD_MODULE_DataExchange=OFF
    -D BUILD_MODULE_Draw=OFF
    -D "BUILD_ADDITIONAL_TOOLKITS=$OCCT_TOOLKITS"
    -D BUILD_DOC_Overview=OFF
    -D BUILD_GTEST=OFF
    -D BUILD_USE_PCH=ON
    -D USE_FREETYPE=OFF
    -D USE_TK=OFF
    -D USE_FREEIMAGE=OFF
    -D USE_OPENGL=OFF
    -D USE_TBB=OFF
    -D USE_RAPIDJSON=OFF
    -D USE_DRACO=OFF
    -D USE_VTK=OFF
    -D USE_EIGEN=OFF
)

case "$target" in
    macos)
        args+=(-D CMAKE_OSX_ARCHITECTURES=arm64
            -D CMAKE_OSX_DEPLOYMENT_TARGET="$OCCT_APPLE_DEPLOYMENT_TARGET")
        ;;
    ios | ios-simulator)
        sysroot=iphoneos
        [[ "$target" == ios-simulator ]] && sysroot=iphonesimulator
        args+=(-D CMAKE_SYSTEM_NAME=iOS
            -D CMAKE_OSX_SYSROOT="$sysroot"
            -D CMAKE_OSX_ARCHITECTURES=arm64
            -D CMAKE_OSX_DEPLOYMENT_TARGET="$OCCT_APPLE_DEPLOYMENT_TARGET")
        ;;
    wasm)
        [[ -n "${EMSDK:-}" ]] || die "EMSDK is not set; activate emsdk first (see docs/BUILDING.md)"
        # Native WebAssembly exceptions (OCCT reports failures with C++ exceptions) and SIMD.
        # OCC_CONVERT_SIGNALS is turned off: it wraps OCC_CATCH_SIGNALS blocks in setjmp/longjmp,
        # and combined with -fwasm-exceptions, Emscripten 6.0.12 emits invalid WebAssembly for
        # some functions ("br_table: label arity inconsistent"). WebAssembly has no signals to
        # convert, so nothing is lost.
        wasm_flags="-fwasm-exceptions -msimd128 -UOCC_CONVERT_SIGNALS"
        args+=(-D CMAKE_TOOLCHAIN_FILE="$EMSDK/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake"
            -D "CMAKE_CXX_FLAGS=$wasm_flags"
            -D "CMAKE_C_FLAGS=$wasm_flags")
        ;;
esac

log "Configuring $target"
cmake "${args[@]}" >"$OCCT_WORK_DIR/$target-configure.log" 2>&1 \
    || { tail -40 "$OCCT_WORK_DIR/$target-configure.log"; die "configure failed"; }

log "Building $target with $(cpu_count) jobs"
started=$SECONDS
cmake --build "$build_dir" --parallel "$(cpu_count)"
cmake --install "$build_dir" >/dev/null
log "Built $target in $(((SECONDS - started) / 60)) min: $prefix"
