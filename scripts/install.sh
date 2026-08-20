#!/usr/bin/env bash
# Install this project's own build artifacts to
# out/install/<platform>-<arch>-<compiler>/ (INSTALL_OUTPUT_DIR in
# Configuration.cmake; see CMakeLists.txt's install() DESTINATION paths).
#
# Always passes --component to `cmake --install`. Without it, any
# vendored/fetched dependency's own install() rules (see
# cmake/Dependencies.cmake) would also run, typically dumping files under
# CMAKE_INSTALL_PREFIX (e.g. /usr/local) -- COMPONENT is what keeps
# `cmake --install` scoped to just this project's own targets.
#
# Usage (from the project root):
#   scripts/install.sh [BUILD_TYPE]
#
#   BUILD_TYPE  Debug|Release|RelWithDebInfo|MinSizeRel (default: Release) --
#               only relevant for a Ninja Multi-Config build.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR/.."

BUILD_DIR="${BUILD_DIR:-out/build}"
BUILD_TYPE="${1:-Release}"
COMPONENT="demo"  # must match PROJECT_CODE_NAME in CMakeLists.txt

if [ ! -d "$BUILD_DIR" ]; then
    echo "error: '${BUILD_DIR}' not found. Run scripts/build.sh first." >&2
    exit 1
fi

echo "==> Installing (component: ${COMPONENT})"
cmake --install "${BUILD_DIR}" --config "${BUILD_TYPE}" --component "${COMPONENT}"
