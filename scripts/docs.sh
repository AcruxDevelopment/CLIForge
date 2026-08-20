#!/usr/bin/env bash
# Build Doxygen documentation for every module (see cmake/Documentation.cmake)
# and print the path to the landing page. A no-op with a clear message if
# Doxygen isn't installed or ENABLE_DOXYGEN=OFF.
#
# Output goes to out/docs/ (DOCS_OUTPUT_DIR in Configuration.cmake) --
# note this is a sibling of out/build, not nested inside it. If you've
# overridden DOCS_OUTPUT_DIR at configure time, also set DOCS_DIR here to
# match so the printed path is correct (docs still land in the right
# place either way -- this only affects the message below).
#
# Usage (from the project root):
#   scripts/docs.sh              # every module
#   scripts/docs.sh core_lib      # just one module's docs (docs_<target>)
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR/.."

BUILD_DIR="${BUILD_DIR:-out/build}"
DOCS_DIR="${DOCS_DIR:-out/docs}"

if [ ! -d "$BUILD_DIR" ]; then
    echo "error: '${BUILD_DIR}' not found. Run scripts/build.sh first." >&2
    exit 1
fi

if [ $# -ge 1 ]; then
    TARGET="docs_$1"
    cmake --build "${BUILD_DIR}" --target "${TARGET}"
    echo "==> Open: ${DOCS_DIR}/$1/html/index.html"
else
    cmake --build "${BUILD_DIR}" --target docs
    if [ -f "${DOCS_DIR}/index.html" ]; then
        echo "==> Open: ${DOCS_DIR}/index.html"
    fi
fi
