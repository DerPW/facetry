#!/usr/bin/env bash
# Downloads a prebuilt OpenCASCADE artifact from this repository's GitHub releases.
#
#   third_party/occt/fetch.sh apple|wasm
#
# Installs into third_party/_install/<artifact>/<kind>. Exits with 2 if the release has no such
# artifact yet, so callers can fall back to build.sh + package.sh.
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

kind="${1:-}"
[[ "$kind" == apple || "$kind" == wasm ]] || die "usage: fetch.sh apple|wasm"

dest="$OCCT_INSTALL_ROOT/$kind"
if [[ -f "$dest/BUILDINFO.txt" ]]; then
    log "$OCCT_ARTIFACT-$kind is already installed"
    exit 0
fi

name="$OCCT_ARTIFACT-$kind.tar.gz"
base="https://github.com/$OCCT_RELEASE_REPO/releases/download/$OCCT_ARTIFACT"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

log "Downloading $name"
if ! curl -fsSL -o "$tmp/$name.sha256" "$base/$name.sha256"; then
    log "No prebuilt $name in release $OCCT_ARTIFACT"
    exit 2
fi
curl -fsSL -o "$tmp/$name" "$base/$name"

expected="$(cut -d' ' -f1 "$tmp/$name.sha256")"
actual="$(sha256_of "$tmp/$name")"
[[ "$expected" == "$actual" ]] || die "SHA256 mismatch for $name: expected $expected, got $actual"

mkdir -p "$OCCT_INSTALL_ROOT"
tar -xzf "$tmp/$name" -C "$OCCT_INSTALL_ROOT"
log "Installed $dest"
