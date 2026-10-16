#!/usr/bin/env bash
# Packages built OpenCASCADE libraries into a prebuilt artifact.
#
#   third_party/occt/package.sh apple   # after build.sh macos, ios and ios-simulator
#   third_party/occt/package.sh wasm    # after build.sh wasm
#
# apple: one dynamic OCCT.framework per platform (all toolkits in one library, so the LGPL
#        component stays replaceable), combined into OCCT.xcframework.
# wasm:  the static libraries as they are.
#
# Installs the artifact into third_party/_install/<artifact>/<kind> for CMake and writes
# third_party/_dist/<artifact>-<kind>.tar.gz plus a .sha256 file for publishing.
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

kind="${1:-}"
case "$kind" in
    apple) targets=(macos ios ios-simulator) ;;
    wasm) targets=(wasm) ;;
    *) die "usage: package.sh apple|wasm" ;;
esac

for target in "${targets[@]}"; do
    [[ -d "$OCCT_WORK_DIR/$target/install/lib" ]] || die "missing $target build; run build.sh $target"
done

out="$OCCT_INSTALL_ROOT/$kind"
rm -rf "$out"
mkdir -p "$out/licenses"
cp -R "$OCCT_WORK_DIR/${targets[0]}/install/include" "$out/include"
cp "$OCCT_SRC_DIR/LICENSE_LGPL_21.txt" "$OCCT_SRC_DIR/OCCT_LGPL_EXCEPTION.txt" "$out/licenses/"

write_framework_plist() {
    local path="$1" platform="$2" min_os_key="$3"
    cat >"$path" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleDevelopmentRegion</key>
    <string>en</string>
    <key>CFBundleExecutable</key>
    <string>OCCT</string>
    <key>CFBundleIdentifier</key>
    <string>io.github.derpw.facetry.occt</string>
    <key>CFBundleInfoDictionaryVersion</key>
    <string>6.0</string>
    <key>CFBundleName</key>
    <string>OCCT</string>
    <key>CFBundlePackageType</key>
    <string>FMWK</string>
    <key>CFBundleShortVersionString</key>
    <string>${OCCT_VERSION}</string>
    <key>CFBundleVersion</key>
    <string>${OCCT_VERSION}.${OCCT_BUILD_REV}</string>
    <key>CFBundleSupportedPlatforms</key>
    <array>
        <string>${platform}</string>
    </array>
    <key>${min_os_key}</key>
    <string>${OCCT_APPLE_DEPLOYMENT_TARGET}</string>
    <key>NSHumanReadableCopyright</key>
    <string>Open CASCADE Technology, LGPL-2.1 with the OCCT exception</string>
</dict>
</plist>
EOF
}

# make_framework <target> <sdk> <clang target triple>
make_framework() {
    local target="$1" sdk="$2" triple="$3"
    local stage="$OCCT_WORK_DIR/$target/framework"
    local fw="$stage/OCCT.framework"
    local libs=("$OCCT_WORK_DIR/$target/install/lib/"*.a)
    local sysroot binary install_name plist_dir platform min_os_key
    local system_libs=()
    sysroot="$(xcrun --sdk "$sdk" --show-sdk-path)"
    rm -rf "$stage"

    if [[ "$target" == macos ]]; then
        # macOS frameworks use the versioned (deep) bundle layout.
        mkdir -p "$fw/Versions/A/Resources"
        binary="$fw/Versions/A/OCCT"
        plist_dir="$fw/Versions/A/Resources"
        install_name="@rpath/OCCT.framework/Versions/A/OCCT"
        system_libs=(-framework Foundation -framework AppKit -framework IOKit -lobjc)
        platform=MacOSX
        min_os_key=LSMinimumSystemVersion
    else
        mkdir -p "$fw"
        binary="$fw/OCCT"
        plist_dir="$fw"
        install_name="@rpath/OCCT.framework/OCCT"
        system_libs=(-framework Foundation -framework UIKit -lobjc)
        platform=iPhoneOS
        [[ "$target" == ios-simulator ]] && platform=iPhoneSimulator
        min_os_key=MinimumOSVersion
    fi

    log "Linking OCCT.framework for $target (${#libs[@]} toolkits)"
    xcrun --sdk "$sdk" clang++ -target "$triple" -isysroot "$sysroot" -dynamiclib \
        -install_name "$install_name" \
        -current_version "$OCCT_VERSION" -compatibility_version "${OCCT_VERSION%.*}.0" \
        -Wl,-all_load "${libs[@]}" "${system_libs[@]}" \
        -o "$binary"
    xcrun strip -x "$binary"
    write_framework_plist "$plist_dir/Info.plist" "$platform" "$min_os_key"

    if [[ "$target" == macos ]]; then
        ln -s A "$fw/Versions/Current"
        ln -s Versions/Current/OCCT "$fw/OCCT"
        ln -s Versions/Current/Resources "$fw/Resources"
    fi
    # Stripping invalidates the linker's ad-hoc signature, which arm64 requires. Sign the whole
    # bundle with its bundle ID as identifier: devices refuse frameworks whose identifier differs,
    # and apps keep the identifier when they re-sign the framework with their own identity.
    # (Artifacts up to r1 lack this; apps/apple/scripts/build-core.sh fixes them.)
    xcrun codesign --force --sign - --identifier io.github.derpw.facetry.occt "$fw"
}

case "$kind" in
    apple)
        make_framework macos macosx "arm64-apple-macos${OCCT_APPLE_DEPLOYMENT_TARGET}"
        make_framework ios iphoneos "arm64-apple-ios${OCCT_APPLE_DEPLOYMENT_TARGET}"
        make_framework ios-simulator iphonesimulator \
            "arm64-apple-ios${OCCT_APPLE_DEPLOYMENT_TARGET}-simulator"
        log "Creating OCCT.xcframework"
        xcodebuild -create-xcframework \
            -framework "$OCCT_WORK_DIR/macos/framework/OCCT.framework" \
            -framework "$OCCT_WORK_DIR/ios/framework/OCCT.framework" \
            -framework "$OCCT_WORK_DIR/ios-simulator/framework/OCCT.framework" \
            -output "$out/OCCT.xcframework" >/dev/null
        ;;
    wasm)
        mkdir -p "$out/lib"
        cp "$OCCT_WORK_DIR/wasm/install/lib/"*.a "$out/lib/"
        ;;
esac

{
    echo "artifact: $OCCT_ARTIFACT-$kind"
    echo "occt_version: $OCCT_VERSION ($OCCT_TAG)"
    echo "build_rev: $OCCT_BUILD_REV"
    echo "requested_toolkits: $OCCT_TOOLKITS"
    echo "built_toolkits: $(cd "$OCCT_WORK_DIR/${targets[0]}/install/lib" && ls ./*.a | sed -E 's#^\./lib##; s#\.a$##' | tr '\n' ' ')"
    echo "targets: ${targets[*]}"
    echo "facetry_commit: $(git -C "$FACETRY_ROOT" rev-parse --short HEAD 2>/dev/null || echo unknown)"
    echo "built_on: $(uname -sm), $(date -u +%Y-%m-%dT%H:%MZ)"
} >"$out/BUILDINFO.txt"

mkdir -p "$OCCT_DIST_DIR"
tarball="$OCCT_DIST_DIR/$OCCT_ARTIFACT-$kind.tar.gz"
tar -czf "$tarball" -C "$OCCT_INSTALL_ROOT" "$kind"
echo "$(sha256_of "$tarball")  $(basename "$tarball")" >"$tarball.sha256"
log "Installed $out"
log "Artifact $tarball ($(du -h "$tarball" | cut -f1))"
