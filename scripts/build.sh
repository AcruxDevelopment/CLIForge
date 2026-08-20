#!/usr/bin/env bash
# Configure (if needed) and build the project with Ninja.
#
# Usage (from the project root):
#   scripts/build.sh [BUILD_TYPE]
#
#   BUILD_TYPE  Debug|Release|RelWithDebInfo|MinSizeRel (default: Release)
#
# Set BUILD_DIR to build somewhere other than out/build (see Configuration.cmake).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR/.."

BUILD_DIR="${BUILD_DIR:-out/build}"
BUILD_TYPE="${1:-Release}"

command -v cmake >/dev/null 2>&1 || { echo "error: cmake not found on PATH." >&2; exit 1; }
command -v ninja >/dev/null 2>&1 || { echo "error: ninja not found on PATH." >&2; exit 1; }

echo "==> Configuring (${BUILD_TYPE}) into ${BUILD_DIR}/"
cmake -G Ninja -S . -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE="${BUILD_TYPE}"

echo "==> Building"
cmake --build "${BUILD_DIR}"

echo "==> Done. Run a module with:  scripts/run.sh <target-name>"
