# =============================================================================
# Configuration.cmake
#
# All output-path configuration lives here. Change a value here, or
# override with -D<NAME>=... at configure time, instead of hunting through
# CMakeLists.txt/cmake/*.cmake for a hardcoded path.
#
# Everything this project generates lives under one OUT_DIR:
#
#   out/
#     build/                           CMAKE_BINARY_DIR -- scripts/build.sh
#                                       defaults to configuring here (-B
#                                       out/build); compiled artifacts land
#                                       in out/build/<RUNTIME_OUTPUT_SUBDIR>
#                                       and out/build/<ARCHIVE_OUTPUT_SUBDIR>
#                                       during normal day-to-day builds.
#     install/<DIST_TARGET>/{bin,lib}  what `cmake --install` / scripts/
#                                       scripts/install.sh populates.
#     dist/                            reserved for packaged/archived
#                                       output (e.g. a future CPack
#                                       integration) -- not populated by
#                                       this project yet, but the path is
#                                       configured and ready.
#     docs/<module>/html, docs/index.html   generated Doxygen output
#                                       (cmake/Documentation.cmake).
#
# One thing lives outside OUT_DIR on purpose: .cache/tools/ (TOOLCACHE_DIR)
# caches downloaded, pinned build tools (currently a pinned Doxygen), kept
# separate so a clean `rm -rf out/` doesn't force a re-download.
#
# Included by CMakeLists.txt before any output-directory variable is used,
# so every value here is available project-wide.
# =============================================================================

include_guard(GLOBAL)

set(OUT_DIR "${CMAKE_SOURCE_DIR}/out" CACHE PATH
    "Root directory for everything this project generates: build artifacts, install trees, docs, and reserved dist packaging output")

set(DIST_OUTPUT_DIR "${OUT_DIR}/dist" CACHE PATH
    "Reserved for packaged/archived distribution output (e.g. a future CPack integration). Not populated by this project yet.")

set(INSTALL_OUTPUT_DIR "${OUT_DIR}/install" CACHE PATH
    "Root of the cmake --install destination tree, one subdirectory per DIST_TARGET")

set(DOCS_OUTPUT_DIR "${OUT_DIR}/docs" CACHE PATH
    "Root of generated Doxygen documentation, one subdirectory per module")

# Deliberately NOT under OUT_DIR: this caches downloaded, pinned build
# tools (currently just Doxygen -- see cmake/Documentation.cmake), which
# are machine-local and expensive to re-fetch, not build output. Keeping
# it separate means a clean `rm -rf out/` (a normal "start fresh" gesture)
# doesn't force a re-download.
set(TOOLCACHE_DIR "${CMAKE_SOURCE_DIR}/.cache/tools" CACHE PATH
    "Where pinned build tools (currently just a downloaded Doxygen) are cached, kept separate from OUT_DIR")

# out/dist/ is reserved (see the header comment above) and nothing writes
# to it yet, so it's created eagerly here -- otherwise it simply wouldn't
# exist until something populates it, which could easily read as "this
# was never wired up" rather than "intentionally reserved for later".
file(MAKE_DIRECTORY "${DIST_OUTPUT_DIR}")

# Subdirectory NAMES only (not full paths) for where compiled artifacts
# land inside whatever the current build directory is (CMAKE_BINARY_DIR) --
# these apply correctly no matter where -B points, including if you build
# outside out/build entirely. LIBRARY defaults to the same subdirectory as
# RUNTIME because shared libraries need to sit next to the executables
# that load them via the project's $ORIGIN-relative RPATH strategy (see
# CMakeLists.txt).
set(RUNTIME_OUTPUT_SUBDIR "bin" CACHE STRING "Build-dir subdirectory for runtime artifacts (executables, .dll, .so)")
set(LIBRARY_OUTPUT_SUBDIR "bin" CACHE STRING "Build-dir subdirectory for shared-library artifacts")
set(ARCHIVE_OUTPUT_SUBDIR "lib" CACHE STRING "Build-dir subdirectory for static/import-library artifacts (.a, .lib)")
