#!/usr/bin/env bash
# Builds the frameworks the Apple app links, into apps/apple/Frameworks (not checked in):
#   CFacetryCore.xcframework  the core as a static library plus its C API as a Clang module
#   OCCT.xcframework          OpenCASCADE, copied from third_party/_install (embedded in the app)
#
# Run it after changing core code, then build the app in Xcode. Needs the OpenCASCADE artifact
# (third_party/occt/fetch.sh apple).
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
out="$root/apps/apple/Frameworks"
stage="$root/build/apple-core"

# shellcheck source=../../../third_party/occt/occt.env
source "$root/third_party/occt/occt.env"
occt="$root/third_party/_install/occt-${OCCT_VERSION}-r${OCCT_BUILD_REV}/apple/OCCT.xcframework"
[[ -d "$occt" ]] || { echo "Missing $occt; run third_party/occt/fetch.sh apple" >&2; exit 1; }

presets=(macos-release ios-device ios-simulator)
for preset in "${presets[@]}"; do
    echo "== Building facetry-core ($preset)"
    cmake --preset "$preset" >/dev/null
    cmake --build --preset "$preset" --target facetry-core
done

# Swift imports the C API as the module CFacetryCore.
rm -rf "$stage"
mkdir -p "$stage/include"
cp "$root/core/include/facetry_core.h" "$stage/include/"
cat >"$stage/include/module.modulemap" <<'EOF'
module CFacetryCore {
    header "facetry_core.h"
    export *
}
EOF

mkdir -p "$out"
rm -rf "$out/CFacetryCore.xcframework" "$out/OCCT.xcframework"
args=()
for preset in "${presets[@]}"; do
    args+=(-library "$root/build/$preset/core/libfacetry-core.a" -headers "$stage/include")
done
xcodebuild -create-xcframework "${args[@]}" -output "$out/CFacetryCore.xcframework" >/dev/null
ditto "$occt" "$out/OCCT.xcframework"
echo "== Frameworks ready in $out"
