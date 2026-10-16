#!/usr/bin/env bash
# Runs a command-line test binary inside the iOS simulator (used as CMAKE_CROSSCOMPILING_EMULATOR).
#
# Uses FACETRY_SIMULATOR_UDID if set. Otherwise it picks an already booted iPad (or other iOS
# device) whose iOS version is at least the deployment target, and boots the newest available
# iPad if none is booted.
set -euo pipefail

min_ios="${FACETRY_SIMULATOR_MIN_IOS:-26}"

pick_device() {
    # $1: "booted" or "available"
    xcrun simctl list devices "$1" -j | python3 -c '
import json, re, sys
state, min_major = sys.argv[1], int(sys.argv[2])
runtimes = json.load(sys.stdin)["devices"]
candidates = []
for runtime, devices in runtimes.items():
    match = re.search(r"\.SimRuntime\.iOS-(\d+)-(\d+)", runtime)
    if not match or int(match.group(1)) < min_major:
        continue
    version = (int(match.group(1)), int(match.group(2)))
    for device in devices:
        is_ipad = device["name"].startswith("iPad")
        if state == "available" and not is_ipad:
            continue
        candidates.append((is_ipad, version, device["udid"]))
candidates.sort(reverse=True)
print(candidates[0][2] if candidates else "")
' "$1" "$min_ios"
}

udid="${FACETRY_SIMULATOR_UDID:-}"
[[ -n "$udid" ]] || udid="$(pick_device booted)"

if [[ -z "$udid" ]]; then
    udid="$(pick_device available)"
    [[ -n "$udid" ]] || { echo "No iPad simulator with iOS $min_ios or newer available" >&2; exit 1; }
    echo "Booting iPad simulator $udid" >&2
    xcrun simctl boot "$udid"
    xcrun simctl bootstatus "$udid" -b >/dev/null
fi

exec xcrun simctl spawn "$udid" "$@"
