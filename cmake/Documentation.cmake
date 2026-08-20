# =============================================================================
# Documentation.cmake
#
# Automatic, per-module Doxygen documentation, using a pinned Doxygen
# version this project downloads and manages itself -- never your
# system's doxygen, so every machine gets identical output. Zero setup
# required: every module registered via add_lib_module/add_exe_module/
# add_test_module/add_example_module gets its own documentation target
# for free -- nothing to add to a module's own CMakeLists.txt. Vendored
# and fetched dependencies (cmake/Dependencies.cmake) are never
# auto-documented, same reasoning as their compile_commands.json
# exclusion: it's not your code.
#
# What gets generated, per module <target>:
#   out/docs/<target>/html/index.html   -- via `cmake --build <build-dir> --target docs_<target>`
# Plus one landing page linking to all of them:
#   out/docs/index.html                 -- via `cmake --build <build-dir> --target docs`
# (or just run scripts/docs.sh / docs.bat, which builds `docs` and prints the path)
# Paths above use the defaults from Configuration.cmake (DOCS_OUTPUT_DIR).
#
# Configuring it:
#   -DENABLE_DOXYGEN=OFF                disable entirely (skips the download too)
#   -DDOXYGEN_PINNED_VERSION=1.17.0     use a different pinned version (also
#       update the two SHA256 hashes below to match that release's assets)
#   -DDOXYGEN_WARN_IF_UNDOCUMENTED=YES / -DDOXYGEN_QUIET=NO   stricter/louder output
#   -DDOXYGEN_<ANY_DOXYFILE_TAG>=...    any Doxyfile tag can be set this way --
#       see https://www.doxygen.nl/manual/config.html
# A module can opt out with NO_DOCS, e.g.:
#   add_lib_module(core_lib TYPE SHARED NO_DOCS ...)
# =============================================================================

include_guard(GLOBAL)

# -----------------------------------------------------------------------
# Pinned Doxygen -- deliberately NOT found via find_package(Doxygen)
# searching the system. Every machine building this project downloads and
# uses the exact same Doxygen version, once, cached outside out/ (in
# TOOLCACHE_DIR, see Configuration.cmake) so a clean out/ rebuild doesn't
# force a re-download. Bump DOXYGEN_PINNED_VERSION to change the pinned
# version project-wide -- doing so also requires updating the two SHA256
# hashes below to match the new release's assets (a stale hash fails loud
# and hard by design; see the integrity check further down).
# -----------------------------------------------------------------------
set(DOXYGEN_PINNED_VERSION "1.16.1" CACHE STRING
    "Exact Doxygen version this project downloads and uses -- never a system-installed doxygen")

option(ENABLE_DOXYGEN "Generate per-module Doxygen documentation using a pinned, project-managed Doxygen (downloaded automatically)" ON)

if(ENABLE_DOXYGEN)
    # Loads the FindDoxygen module (defines doxygen_add_docs(), used
    # below) and, as a side effect, searches the system for `dot`
    # (Graphviz) for call graphs -- an optional, system-detected
    # enhancement. This call's own doxygen-executable detection is
    # discarded and overridden with our pinned copy regardless of what
    # (if anything) it finds; only `dot` detection from this is used.
    find_package(Doxygen QUIET OPTIONAL_COMPONENTS dot)

    string(REPLACE "." "_" _pmn_doxygen_tag_version "${DOXYGEN_PINNED_VERSION}")
    set(_pmn_doxygen_tag "Release_${_pmn_doxygen_tag_version}")
    set(_pmn_doxygen_root "${TOOLCACHE_DIR}/doxygen-${DOXYGEN_PINNED_VERSION}")

    if(CMAKE_HOST_WIN32)
        set(_pmn_doxygen_url "https://github.com/doxygen/doxygen/releases/download/${_pmn_doxygen_tag}/doxygen-${DOXYGEN_PINNED_VERSION}.windows.x64.bin.zip")
        set(_pmn_doxygen_expected_hash "ddfa59a4ae9549651330471c2e387ec7a9891080543faed541c859e5ce448653")
        set(_pmn_doxygen_exe "${_pmn_doxygen_root}/doxygen.exe")
    elseif(CMAKE_HOST_UNIX AND NOT CMAKE_HOST_APPLE)
        set(_pmn_doxygen_url "https://github.com/doxygen/doxygen/releases/download/${_pmn_doxygen_tag}/doxygen-${DOXYGEN_PINNED_VERSION}.linux.bin.tar.gz")
        set(_pmn_doxygen_expected_hash "a56f885d37e3aae08a99f638d17bbb381224c03a878d9e2dda4f9fa4baf1d8bd")
        set(_pmn_doxygen_exe "${_pmn_doxygen_root}/doxygen-${DOXYGEN_PINNED_VERSION}/bin/doxygen")
    else()
        message(WARNING "No pinned Doxygen ${DOXYGEN_PINNED_VERSION} binary is configured for this host "
                         "platform. Install Doxygen ${DOXYGEN_PINNED_VERSION} manually and pass "
                         "-DDOXYGEN_EXECUTABLE=/path/to/doxygen, or extend cmake/Documentation.cmake "
                         "with a download URL + hash for this platform.")
        set(ENABLE_DOXYGEN OFF)
    endif()

    if(ENABLE_DOXYGEN AND NOT EXISTS "${_pmn_doxygen_exe}")
        message(STATUS "Downloading pinned Doxygen ${DOXYGEN_PINNED_VERSION} (one-time; cached under ${TOOLCACHE_DIR})")
        set(_pmn_doxygen_archive "${TOOLCACHE_DIR}/_doxygen_download.archive")
        # No EXPECTED_HASH here on purpose: file(DOWNLOAD ... EXPECTED_HASH
        # ...) raises a hard, unrecoverable CMake Error on ANY failure --
        # including plain network/connectivity failures -- which would
        # defeat the graceful "disable docs, keep building" fallback below.
        # Downloading first and verifying the hash ourselves afterward
        # (further down) lets us tell a network hiccup (recoverable, just
        # disable docs for this configure) apart from a genuine integrity
        # failure (not recoverable -- see the FATAL_ERROR below).
        file(DOWNLOAD "${_pmn_doxygen_url}" "${_pmn_doxygen_archive}"
            STATUS _pmn_doxygen_dl_status
            SHOW_PROGRESS
        )
        list(GET _pmn_doxygen_dl_status 0 _pmn_doxygen_dl_code)
        if(NOT _pmn_doxygen_dl_code EQUAL 0)
            list(GET _pmn_doxygen_dl_status 1 _pmn_doxygen_dl_msg)
            file(REMOVE "${_pmn_doxygen_archive}")
            message(WARNING "Failed to download pinned Doxygen ${DOXYGEN_PINNED_VERSION}: "
                             "${_pmn_doxygen_dl_msg}. Documentation will be disabled for this configure "
                             "-- everything else builds normally. Reconfigure once you have network "
                             "access to retry.")
            set(ENABLE_DOXYGEN OFF)
        else()
            # Verify integrity ourselves, deliberately as a hard failure --
            # a mismatch here means the downloaded artifact doesn't match
            # what this project expects (corruption or tampering in
            # transit, or a stale pinned hash after bumping
            # DOXYGEN_PINNED_VERSION without updating it): a supply-chain-
            # relevant integrity failure, not something to silently
            # degrade past the way a plain network failure above is.
            file(SHA256 "${_pmn_doxygen_archive}" _pmn_doxygen_actual_hash)
            string(TOLOWER "${_pmn_doxygen_actual_hash}" _pmn_doxygen_actual_hash)
            string(TOLOWER "${_pmn_doxygen_expected_hash}" _pmn_doxygen_expected_hash_lc)
            if(NOT _pmn_doxygen_actual_hash STREQUAL _pmn_doxygen_expected_hash_lc)
                file(REMOVE "${_pmn_doxygen_archive}")
                message(FATAL_ERROR "Pinned Doxygen ${DOXYGEN_PINNED_VERSION} download hash mismatch -- "
                                     "expected ${_pmn_doxygen_expected_hash_lc}, got "
                                     "${_pmn_doxygen_actual_hash}. This usually means "
                                     "DOXYGEN_PINNED_VERSION was bumped without updating the matching "
                                     "hash in cmake/Documentation.cmake, or the download was corrupted "
                                     "or tampered with in transit.")
            endif()
            file(ARCHIVE_EXTRACT INPUT "${_pmn_doxygen_archive}" DESTINATION "${_pmn_doxygen_root}")
            file(REMOVE "${_pmn_doxygen_archive}")
            if(NOT CMAKE_HOST_WIN32)
                file(CHMOD "${_pmn_doxygen_exe}" PERMISSIONS
                    OWNER_READ OWNER_WRITE OWNER_EXECUTE
                    GROUP_READ GROUP_EXECUTE
                    WORLD_READ WORLD_EXECUTE)
            endif()
        endif()
    endif()

    if(ENABLE_DOXYGEN AND NOT EXISTS "${_pmn_doxygen_exe}")
        message(WARNING "Pinned Doxygen download/extract did not produce the expected executable at "
                         "${_pmn_doxygen_exe}. Documentation will be disabled for this configure.")
        set(ENABLE_DOXYGEN OFF)
    endif()

    if(ENABLE_DOXYGEN)
        # Override whatever (if anything) find_package(Doxygen) found
        # above -- this project never uses a system-installed doxygen.
        set(DOXYGEN_EXECUTABLE "${_pmn_doxygen_exe}" CACHE FILEPATH "Pinned Doxygen executable (never the system's)" FORCE)
        set(DOXYGEN_VERSION "${DOXYGEN_PINNED_VERSION}")
    endif()
endif()

if(ENABLE_DOXYGEN)
    message(STATUS "Doxygen ${DOXYGEN_VERSION} ready (pinned, not system-installed: ${DOXYGEN_EXECUTABLE}) -- per-module documentation enabled")

    # Project-wide defaults, one Doxyfile tag per DOXYGEN_<TAG> variable.
    # Override any of them with -DDOXYGEN_<TAG>=... at configure time.
    # Kept deliberately quiet and lenient by default (no warnings about
    # undocumented entities) so a freshly generated module with zero
    # doxygen comments still produces clean, useful output -- browsable
    # files/functions/call graphs -- instead of a wall of warnings.
    set(DOXYGEN_GENERATE_HTML YES)
    set(DOXYGEN_GENERATE_LATEX NO)
    set(DOXYGEN_EXTRACT_ALL YES)
    set(DOXYGEN_EXTRACT_STATIC YES)
    set(DOXYGEN_EXTRACT_PRIVATE NO)
    set(DOXYGEN_WARN_IF_UNDOCUMENTED NO)
    set(DOXYGEN_QUIET YES)
    set(DOXYGEN_RECURSIVE YES)
    set(DOXYGEN_SOURCE_BROWSER YES)
    set(DOXYGEN_GENERATE_TREEVIEW YES)
    if(DOXYGEN_DOT_FOUND)
        set(DOXYGEN_HAVE_DOT YES)
        set(DOXYGEN_CALL_GRAPH YES)
        set(DOXYGEN_CALLER_GRAPH YES)
    endif()

    add_custom_target(docs COMMENT "Build documentation for every module")
else()
    add_custom_target(docs
        COMMAND ${CMAKE_COMMAND} -E echo
            "Documentation is disabled (ENABLE_DOXYGEN=OFF, or Doxygen isn't installed). Install Doxygen and reconfigure to enable it."
        VERBATIM
    )
endif()

# -----------------------------------------------------------------------
# _add_module_docs(TARGET): called automatically from _finalize_module_target
# for every module -- creates docs_<TARGET> (a Doxygen run scoped to that
# module's own directory) and wires it into the aggregate `docs` target.
# A no-op if ENABLE_DOXYGEN is off. Excluded from the default build same
# as tests/examples -- documentation is opt-in per build, not generated
# on every compile.
#
# If the module's own directory has a README.md, it's used as that
# module's Doxygen mainpage automatically.
# -----------------------------------------------------------------------
function(_add_module_docs TARGET)
    if(NOT ENABLE_DOXYGEN)
        return()
    endif()

    set(DOXYGEN_OUTPUT_DIRECTORY "${DOCS_OUTPUT_DIR}/${TARGET}")
    set(DOXYGEN_PROJECT_NAME "${TARGET}")
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/README.md")
        set(DOXYGEN_USE_MDFILE_AS_MAINPAGE "${CMAKE_CURRENT_SOURCE_DIR}/README.md")
    else()
        unset(DOXYGEN_USE_MDFILE_AS_MAINPAGE)
    endif()

    doxygen_add_docs(docs_${TARGET}
        "${CMAKE_CURRENT_SOURCE_DIR}"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMENT "Generating documentation for module '${TARGET}'"
    )
    set_target_properties(docs_${TARGET} PROPERTIES EXCLUDE_FROM_ALL TRUE)
    add_dependencies(docs docs_${TARGET})

    set_property(GLOBAL APPEND PROPERTY PMN_DOC_MODULES ${TARGET})
endfunction()

# -----------------------------------------------------------------------
# _write_docs_index(OUTPUT_DIR): call once, after every module has been
# discovered, to write a plain landing page at OUTPUT_DIR/index.html
# linking to each module's own generated docs -- so "where do I start
# reading" has one obvious answer regardless of how many modules exist.
# -----------------------------------------------------------------------
function(_write_docs_index OUTPUT_DIR)
    if(NOT ENABLE_DOXYGEN)
        return()
    endif()
    file(MAKE_DIRECTORY "${OUTPUT_DIR}")
    get_property(_pmn_doc_modules GLOBAL PROPERTY PMN_DOC_MODULES)
    list(SORT _pmn_doc_modules)
    set(_pmn_doc_links "")
    foreach(_pmn_mod ${_pmn_doc_modules})
        string(APPEND _pmn_doc_links "    <li><a href=\"${_pmn_mod}/html/index.html\">${_pmn_mod}</a></li>\n")
    endforeach()
    file(WRITE "${OUTPUT_DIR}/index.html" "<!DOCTYPE html>
<html>
<head><meta charset=\"utf-8\"><title>${PROJECT_NAME} -- module documentation</title></head>
<body>
  <h1>${PROJECT_NAME} module documentation</h1>
  <ul>
${_pmn_doc_links}  </ul>
</body>
</html>
")
endfunction()
