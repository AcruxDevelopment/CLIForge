#!/usr/bin/env bash
# Run an executable module by its CMake target name (as registered in
# that module's own CMakeLists.txt), resolved via the manifest that
# _write_module_manifest() generates at configure time -- so this script
# never needs updating when modules are added, removed, or renamed.
#
# Usage (from the project root):
#   scripts/run.sh <target-name> [-- program-args...]
#
# Examples:
#   scripts/run.sh hello_exe
#   scripts/run.sh farewell_exe -- Ale
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR/.."

BUILD_DIR="${BUILD_DIR:-out/build}"

list_targets() {
    local manifest
    for manifest in "${BUILD_DIR}"/module_manifest-*.txt; do
        [ -e "$manifest" ] || continue
        cut -d= -f1 "$manifest"
    done | sort -u
}

usage() {
    echo "Usage: $0 <cmake-target-name> [-- program-args...]" >&2
    echo >&2
    echo "Available executable targets:" >&2
    local targets
    targets="$(list_targets)"
    if [ -n "$targets" ]; then
        echo "$targets" | sed 's/^/  /' >&2
    else
        echo "  (none found -- did you run scripts/build.sh?)" >&2
    fi
    exit 1
}

[ $# -ge 1 ] || usage

TARGET_NAME="$1"
shift
if [ "${1:-}" = "--" ]; then
    shift
fi

MANIFEST=""
for m in "${BUILD_DIR}"/module_manifest-*.txt; do
    [ -e "$m" ] || continue
    MANIFEST="$m"
    break
done

if [ -z "$MANIFEST" ]; then
    echo "error: no build manifest found in '${BUILD_DIR}'. Run scripts/build.sh first." >&2
    exit 1
fi

BIN_PATH="$(grep "^${TARGET_NAME}=" "$MANIFEST" | head -n1 | cut -d= -f2- || true)"

if [ -z "$BIN_PATH" ]; then
    echo "error: no executable target named '${TARGET_NAME}'." >&2
    echo "Available targets:" >&2
    list_targets | sed 's/^/  /' >&2
    exit 1
fi

if [ ! -x "$BIN_PATH" ]; then
    echo "error: '${BIN_PATH}' was not found or is not executable. Rebuild with scripts/build.sh." >&2
    exit 1
fi

exec "$BIN_PATH" "$@"
