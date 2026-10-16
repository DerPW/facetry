#!/usr/bin/env bash
# Runs a command-line test binary inside the iOS simulator (used as CMAKE_CROSSCOMPILING_EMULATOR).
#
# Uses FACETRY_SIMULATOR_UDID if set, otherwise an already booted iOS simulator (iPads first),
# otherwise boots the first available iPad simulator of the newest iOS runtime.
set -euo pipefail

pick_device() {
    # $1: "booted" or "available"
    xcrun simctl list devices "$1" -j | python3 -c '
import json, sys
runtimes = json.load(sys.stdin)["devices"]
ios = sorted((r for r in runtimes if ".SimRuntime.iOS-" in r), reverse=True)
devices = [d for r in ios for d in runtimes[r]]
devices.sort(key=lambda d: not d["name"].startswith("iPad"))
if sys.argv[1] == "available":
    devices = [d for d in devices if d["name"].startswith("iPad")]
print(devices[0]["udid"] if devices else "")
' "$1"
}

udid="${FACETRY_SIMULATOR_UDID:-}"
[[ -n "$udid" ]] || udid="$(pick_device booted)"

if [[ -z "$udid" ]]; then
    udid="$(pick_device available)"
    [[ -n "$udid" ]] || { echo "No iPad simulator available" >&2; exit 1; }
    echo "Booting iPad simulator $udid" >&2
    xcrun simctl boot "$udid"
    xcrun simctl bootstatus "$udid" -b >/dev/null
fi

exec xcrun simctl spawn "$udid" "$@"
