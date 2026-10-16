#!/usr/bin/env bash
# Compiles render/shaders/viewport.glsl into render/src/shaders/viewport.glsl.h with a pinned
# sokol-shdc. The generated header is checked in; CI runs this script with --check to make sure
# it is up to date.
#
#   render/shaders/generate.sh          regenerate
#   render/shaders/generate.sh --check  fail if the checked-in header is outdated
set -euo pipefail

SHDC_COMMIT=b975d5642093
case "$(uname -s)-$(uname -m)" in
    Darwin-arm64) platform=osx_arm64 sha=2a807d62dffcc67a50a6ab51df57cde2fc8b23a3daf6207177d79e00fbd4086b ;;
    Linux-x86_64) platform=linux sha=f227db5b282dcaa8085796ba83cda217d3114cf34913ca6ba4b0103f25ccc779 ;;
    *) echo "No pinned sokol-shdc for $(uname -s)-$(uname -m)" >&2; exit 1 ;;
esac

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
tool_dir="$root/third_party/_build/tools/sokol-shdc-$SHDC_COMMIT"
shdc="$tool_dir/sokol-shdc"
if [[ ! -x "$shdc" ]]; then
    mkdir -p "$tool_dir"
    curl -fsSL -o "$shdc.part" \
        "https://raw.githubusercontent.com/floooh/sokol-tools-bin/$SHDC_COMMIT/bin/$platform/sokol-shdc"
    actual="$( (sha256sum "$shdc.part" 2>/dev/null || shasum -a 256 "$shdc.part") | cut -d' ' -f1)"
    [[ "$actual" == "$sha" ]] || { echo "sokol-shdc checksum mismatch: $actual" >&2; exit 1; }
    chmod +x "$shdc.part"
    mv "$shdc.part" "$shdc"
fi

# Relative paths: sokol-shdc writes its command line into the header.
cd "$root"
input="render/shaders/viewport.glsl"
output="render/src/shaders/viewport.glsl.h"
slangs="glsl300es:metal_macos:metal_ios:metal_sim"

if [[ "${1:-}" == "--check" ]]; then
    tmp="$(mktemp -d)"
    trap 'rm -rf "$tmp"' EXIT
    "$shdc" --input "$input" --output "$tmp/viewport.glsl.h" --slang "$slangs" --reflection >/dev/null
    # Ignore the recorded command line, which contains the output path.
    if ! diff -q <(grep -v 'sokol-shdc --input' "$tmp/viewport.glsl.h") \
        <(grep -v 'sokol-shdc --input' "$output") >/dev/null; then
        echo "$output is outdated; run render/shaders/generate.sh" >&2
        exit 1
    fi
    echo "Shader header is up to date"
else
    "$shdc" --input "$input" --output "$output" --slang "$slangs" --reflection
    echo "Wrote $output"
fi
