#!/usr/bin/env bash
# Builds the frameworks the Apple app links, into apps/apple/Frameworks (not checked in):
#   CFacetryCore.xcframework  the core and the viewport renderer as one static library, plus
#                             their C APIs (facetry_core.h, facetry_render.h) as a Clang module
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
    cmake --build --preset "$preset" --target facetry-core facetry-render
    libtool -static -o "$root/build/$preset/libfacetry-apple.a" \
        "$root/build/$preset/core/libfacetry-core.a" "$root/build/$preset/render/libfacetry-render.a"
done

# Swift imports the C API as the module CFacetryCore.
rm -rf "$stage"
mkdir -p "$stage/include"
cp "$root/core/include/facetry_core.h" "$root/render/include/facetry_render.h" "$stage/include/"
cat >"$stage/include/module.modulemap" <<'EOF'
module CFacetryCore {
    header "facetry_core.h"
    header "facetry_render.h"
    export *
}
EOF

mkdir -p "$out"
rm -rf "$out/CFacetryCore.xcframework" "$out/OCCT.xcframework"
args=()
for preset in "${presets[@]}"; do
    args+=(-library "$root/build/$preset/libfacetry-apple.a" -headers "$stage/include")
done
xcodebuild -create-xcframework "${args[@]}" -output "$out/CFacetryCore.xcframework" >/dev/null
ditto "$occt" "$out/OCCT.xcframework"

# Devices only install frameworks whose code signing identifier matches their bundle ID, and Xcode
# keeps a framework's identifier when it re-signs it for the app. The OCCT artifact (up to r1) is
# signed with a generated identifier, so sign each slice with its bundle ID here.
for framework in "$out"/OCCT.xcframework/*/OCCT.framework; do
    plist="$framework/Info.plist"
    [[ -f "$plist" ]] || plist="$framework/Resources/Info.plist" # macOS: versioned bundle
    identifier="$(/usr/libexec/PlistBuddy -c "Print :CFBundleIdentifier" "$plist")"
    xcrun codesign --force --sign - --identifier "$identifier" "$framework"
done
echo "== Frameworks ready in $out"
