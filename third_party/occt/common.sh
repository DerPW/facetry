# Shared helpers for the OpenCASCADE scripts. Sourced, not executed.

OCCT_SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FACETRY_ROOT="$(cd "$OCCT_SCRIPT_DIR/../.." && pwd)"

# shellcheck source=occt.env
source "$OCCT_SCRIPT_DIR/occt.env"

OCCT_ARTIFACT="occt-${OCCT_VERSION}-r${OCCT_BUILD_REV}"
OCCT_WORK_DIR="$FACETRY_ROOT/third_party/_build/occt"
OCCT_SRC_DIR="$OCCT_WORK_DIR/src/occt-${OCCT_VERSION}"
OCCT_DIST_DIR="$FACETRY_ROOT/third_party/_dist"
OCCT_INSTALL_ROOT="$FACETRY_ROOT/third_party/_install/$OCCT_ARTIFACT"
OCCT_RELEASE_REPO="${OCCT_RELEASE_REPO:-DerPW/facetry}"

if [[ -t 2 ]]; then
    log() { printf '\033[1m[occt]\033[0m %s\n' "$*" >&2; }
    die() { printf '\033[1;31m[occt]\033[0m %s\n' "$*" >&2; exit 1; }
else
    log() { printf '[occt] %s\n' "$*" >&2; }
    die() { printf '[occt] error: %s\n' "$*" >&2; exit 1; }
fi

sha256_of() {
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1" | cut -d' ' -f1
    else
        shasum -a 256 "$1" | cut -d' ' -f1
    fi
}

cpu_count() {
    getconf _NPROCESSORS_ONLN 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4
}
