# =============================================================================
# BuildHelpers.cmake
#
# Reusable, project-agnostic build machinery: module discovery, per-target
# compiler/RPATH settings, and the module-registration macros (add_lib_module,
# add_exe_module, add_test_module, add_example_module) that a module's own
# CMakeLists.txt calls.
#
# This file is meant to change rarely and hold nothing project-specific.
# New targets should not need edits here, or in the root CMakeLists.txt --
# see the "Module discovery" section of CMakeLists.txt and the per-module
# CMakeLists.txt convention described there.
#
# Expects the including CMakeLists.txt to have already set (from project()
# and the project-identity block): PROJECT_NAME, PROJECT_VERSION,
# PROJECT_CODE_NAME. Multiplatform: MSVC/WIN32 branches guard the
# Windows-only paths; everything else is generic.
# =============================================================================

include_guard(GLOBAL)

# -----------------------------------------------------------------------
# all_tests / all_examples -- aggregate targets so groups can be built
# without CMake target-name collisions:
#   cmake --build . --target all_tests
#   cmake --build . --target all_examples
#
# enable_testing() is required for add_test_module()'s add_test() calls to
# actually be discoverable by ctest -- without it, tests build fine but
# `ctest` reports "No tests were found" with no error anywhere else.
# -----------------------------------------------------------------------
enable_testing()
add_custom_target(all_tests    COMMENT "Build all test targets")
add_custom_target(all_examples COMMENT "Build all example targets")

# -----------------------------------------------------------------------
# _discover_module_subdirectories(ROOT_DIR): add_subdirectory() every
# immediate child of ROOT_DIR that contains its own CMakeLists.txt.
#
# This is the whole mechanism behind "no root edits to add a module": drop
# a new directory with a one-line CMakeLists.txt under one of the known
# module roots (source/ for libraries and apps, source/tests/ or
# source/examples/ for tests and examples) and it is picked up automatically.
# CONFIGURE_DEPENDS means the *next build* reconfigures on its own after a
# module directory is added or removed -- no manual `cmake` re-run needed.
#
# Order between add_subdirectory() calls does not matter for correctness:
# CMake resolves target_link_libraries() names against the whole project
# at generate time, not sequentially, so a module may DEPENDS on a module
# discovered later (verified empirically -- this isn't a guess).
#
# A macro (not a function) so add_subdirectory() behaves exactly as if it
# were called directly from the caller -- no extra scope layer involved.
# -----------------------------------------------------------------------
macro(_discover_module_subdirectories ROOT_DIR)
    if(IS_DIRECTORY "${ROOT_DIR}")
        file(GLOB _pmn_module_candidates CONFIGURE_DEPENDS LIST_DIRECTORIES true "${ROOT_DIR}/*")
        foreach(_pmn_candidate ${_pmn_module_candidates})
            if(IS_DIRECTORY "${_pmn_candidate}" AND EXISTS "${_pmn_candidate}/CMakeLists.txt")
                add_subdirectory("${_pmn_candidate}")
            endif()
        endforeach()
    endif()
endmacro()

# -----------------------------------------------------------------------
# _apply_compile_options: per-target warning flags.
# MSVC path  : /W4 (high warnings) + /permissive- (strict conformance).
# GCC/Clang  : -Wall -Wextra -Wpedantic.
# -----------------------------------------------------------------------
function(_apply_compile_options TARGET)
    if(MSVC)
        target_compile_options(${TARGET} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${TARGET} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

# -----------------------------------------------------------------------
# _apply_rpath: RPATH is an ELF-only concept; skip entirely on Windows.
# -----------------------------------------------------------------------
function(_apply_rpath TARGET RPATH_VAL)
    if(NOT WIN32)
        set_target_properties(${TARGET} PROPERTIES
            BUILD_RPATH   "${RPATH_VAL}"
            INSTALL_RPATH "${RPATH_VAL}"
        )
    endif()
endfunction()

# -----------------------------------------------------------------------
# _set_output_dirs(TARGET RUNTIME_DIR [LIBRARY_DIR]):
#
# CMakeLists.txt sets CMAKE_RUNTIME_OUTPUT_DIRECTORY_<CONFIG> /
# CMAKE_LIBRARY_OUTPUT_DIRECTORY_<CONFIG> globally (once, near the top) so
# every target defaults to the flat bin/ layout. That also means every
# new target's per-config RUNTIME_OUTPUT_DIRECTORY_<CONFIG> /
# LIBRARY_OUTPUT_DIRECTORY_<CONFIG> property is pre-populated at creation
# time -- and a per-config property always wins over the plain
# (config-independent) one. So a module that needs a *different* output
# directory (tests -> bin/tests, examples -> bin/examples) must override
# the per-config properties too, or the override silently has no effect.
# This helper does both in one place.
# -----------------------------------------------------------------------
function(_set_output_dirs TARGET RUNTIME_DIR)
    set(_library_dir "${RUNTIME_DIR}")
    if(ARGC GREATER 2)
        set(_library_dir "${ARGV2}")
    endif()
    set_target_properties(${TARGET} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${RUNTIME_DIR}"
        LIBRARY_OUTPUT_DIRECTORY "${_library_dir}"
    )
    foreach(_cfg Debug Release RelWithDebInfo MinSizeRel)
        string(TOUPPER "${_cfg}" _CFG)
        set_target_properties(${TARGET} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY_${_CFG} "${RUNTIME_DIR}"
            LIBRARY_OUTPUT_DIRECTORY_${_CFG} "${_library_dir}"
        )
    endforeach()
endfunction()

# -----------------------------------------------------------------------
# _register_executable_target(TARGET): tracks every executable module
# (exe/test/example) in a global property so _write_module_manifest()
# can find them all without anything needing to know their names ahead
# of time.
# -----------------------------------------------------------------------
function(_register_executable_target TARGET)
    set_property(GLOBAL APPEND PROPERTY PMN_EXECUTABLE_TARGETS ${TARGET})
endfunction()

# -----------------------------------------------------------------------
# _register_module_target(CATEGORY TARGET): tracks a target under
# CATEGORY (PRIMARY, TEST, or EXAMPLE) in a global property so
# CMakeLists.txt's install() rules can be generated from whatever modules
# actually got discovered, instead of a hardcoded target list that would
# need editing every time a module is added or removed.
# -----------------------------------------------------------------------
function(_register_module_target CATEGORY TARGET)
    set_property(GLOBAL APPEND PROPERTY PMN_MODULE_TARGETS_${CATEGORY} ${TARGET})
endfunction()

# -----------------------------------------------------------------------
# _write_module_manifest(OUTPUT_DIR): call once, after every module has
# been discovered, to emit "<target-name>=<absolute-path-to-binary>"
# lines for every executable module into
# OUTPUT_DIR/module_manifest-<CONFIG>.txt.
#
# scripts/run.sh / run.bat read this file to resolve a CMake target name to its
# actual executable path -- so they never hardcode per-module paths and
# never need editing when a module is added, removed, or renamed.
# $<CONFIG> is part of the filename because file(GENERATE) requires that
# for content (like $<TARGET_FILE:...>) that can differ per build
# configuration under a multi-config generator (e.g. Ninja Multi-Config).
# -----------------------------------------------------------------------
function(_write_module_manifest OUTPUT_DIR)
    get_property(_pmn_targets GLOBAL PROPERTY PMN_EXECUTABLE_TARGETS)
    set(_manifest_content "")
    foreach(_t ${_pmn_targets})
        string(APPEND _manifest_content "${_t}=$<TARGET_FILE:${_t}>\n")
    endforeach()
    file(GENERATE
        OUTPUT "${OUTPUT_DIR}/module_manifest-$<CONFIG>.txt"
        CONTENT "${_manifest_content}"
    )
endfunction()

# -----------------------------------------------------------------------
# _finalize_module_target(TARGET MODULE_NAME [OUTPUT_NAME name] [DEPENDS ...])
#
# The tail every module macro below shares: identity compile definitions,
# warning flags, OUTPUT_NAME, and DEPENDS linking. Centralized so all four
# macros stay in lockstep -- edit once here instead of four times.
# Re-parses the caller's raw ARGN, so it's safe to call as
# _finalize_module_target(${NAME} ${_MODULE_NAME} ${ARGN}) even though
# ARGN also contains keywords (TYPE, EXTRA_SOURCES, ...) this function
# doesn't know about -- those are simply ignored.
# -----------------------------------------------------------------------
# -----------------------------------------------------------------------
# _finalize_module_target(TARGET MODULE_NAME [OUTPUT_NAME name] [DEPENDS ...]
#                          [NO_DOCS])
#
# The tail every module macro below shares: identity compile definitions,
# warning flags, OUTPUT_NAME, DEPENDS linking, and an automatic per-module
# Doxygen target (see cmake/Documentation.cmake) unless NO_DOCS is given.
# Centralized so all four macros stay in lockstep -- edit once here
# instead of four times. Re-parses the caller's raw ARGN, so it's safe to
# call as _finalize_module_target(${NAME} ${_MODULE_NAME} ${ARGN}) even
# though ARGN also contains keywords (TYPE, EXTRA_SOURCES, ...) this
# function doesn't know about -- those are simply ignored.
# -----------------------------------------------------------------------
function(_finalize_module_target TARGET MODULE_NAME)
    cmake_parse_arguments(FIN "NO_DOCS" "OUTPUT_NAME" "DEPENDS" ${ARGN})

    target_compile_definitions(${TARGET} PRIVATE
        $<$<CONFIG:Debug>:DEBUG>
        MODULE_NAME="${MODULE_NAME}"
        PROJECT_NAME="${PROJECT_NAME}"
        PROJECT_NAME_CODE="${PROJECT_CODE_NAME}"
        PROJECT_VER="${PROJECT_VERSION}"
    )
    _apply_compile_options(${TARGET})

    if(FIN_OUTPUT_NAME)
        set_target_properties(${TARGET} PROPERTIES OUTPUT_NAME ${FIN_OUTPUT_NAME})
    endif()
    if(FIN_DEPENDS)
        target_link_libraries(${TARGET} PRIVATE ${FIN_DEPENDS})
    endif()
    if(NOT FIN_NO_DOCS)
        _add_module_docs(${TARGET})
    endif()
endfunction()

# =============================================================================
# add_lib_module(NAME [TYPE STATIC|SHARED] [OUTPUT_NAME name] [DEPENDS ...]
#                 [EXTRA_SOURCES ...] [NO_DOCS])
#
# Called from a library module's own CMakeLists.txt (e.g. source/Core/CMakeLists.txt).
# Sources/headers are globbed relative to that module's own directory:
# src/*.cpp and include/*.h(pp) underneath it. TYPE picks STATIC or SHARED
# for this module only; omit it to use the project-wide default
# (BUILD_SHARED_LIBS -- set in CMakeLists.txt's OPTIONS section, or pass
# -DBUILD_SHARED_LIBS=ON at configure time).
#
# Headers are globbed in purely so they show up in IDE project trees --
# CMake never compiles headers, so this is inert for the actual build.
# =============================================================================
macro(add_lib_module NAME)
    cmake_parse_arguments(ARG "" "OUTPUT_NAME;TYPE" "DEPENDS;EXTRA_SOURCES" ${ARGN})

    if(ARG_OUTPUT_NAME)
        set(_MODULE_NAME ${ARG_OUTPUT_NAME})
    else()
        set(_MODULE_NAME ${NAME})
    endif()

    if(ARG_TYPE)
        set(_LIB_TYPE ${ARG_TYPE})
    elseif(BUILD_SHARED_LIBS)
        set(_LIB_TYPE SHARED)
    else()
        set(_LIB_TYPE STATIC)
    endif()
    if(NOT _LIB_TYPE MATCHES "^(STATIC|SHARED)$")
        message(FATAL_ERROR "add_lib_module(${NAME}): TYPE must be STATIC or SHARED, got '${_LIB_TYPE}'")
    endif()

    file(GLOB_RECURSE ${NAME}_SOURCES CONFIGURE_DEPENDS src/*.cpp)
    file(GLOB_RECURSE ${NAME}_HEADERS CONFIGURE_DEPENDS include/*.h include/*.hpp)

    add_library(${NAME} ${_LIB_TYPE} ${${NAME}_SOURCES} ${${NAME}_HEADERS} ${ARG_EXTRA_SOURCES})
    target_include_directories(${NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

    if(_LIB_TYPE STREQUAL "STATIC")
        # Keep static libs relocatable in case they end up linked into a
        # shared library or PIE executable later.
        set_target_properties(${NAME} PROPERTIES POSITION_INDEPENDENT_CODE ON)
    else()
        # LIBRARY_OUTPUT_DIRECTORY controls .so placement on Linux.
        # RUNTIME_OUTPUT_DIRECTORY controls .dll placement on Windows.
        # WINDOWS_EXPORT_ALL_SYMBOLS auto-generates a .def file so all
        # public symbols are exported without __declspec(dllexport).
        # Remove if you prefer explicit visibility macros instead.
        _set_output_dirs(${NAME} "${CMAKE_BINARY_DIR}/${RUNTIME_OUTPUT_SUBDIR}" "${CMAKE_BINARY_DIR}/${LIBRARY_OUTPUT_SUBDIR}")
        set_target_properties(${NAME} PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
    endif()

    _register_module_target(PRIMARY ${NAME})
    _finalize_module_target(${NAME} ${_MODULE_NAME} ${ARGN})
endmacro()

# Back-compat wrappers for the old two-macro spelling.
# Prefer add_lib_module(... TYPE STATIC|SHARED) directly in new code.
macro(add_static_lib_module NAME)
    add_lib_module(${NAME} TYPE STATIC ${ARGN})
endmacro()
macro(add_shared_lib_module NAME)
    add_lib_module(${NAME} TYPE SHARED ${ARGN})
endmacro()

# =============================================================================
# add_exe_module(NAME [OUTPUT_NAME name] [DEPENDS ...] [EXTRA_SOURCES ...] [NO_DOCS])
#
# Called from an app module's own CMakeLists.txt (e.g. HelloApp/CMakeLists.txt).
# Sources/headers are globbed relative to that module's own directory:
# *.cpp and include/*.h(pp) underneath it.
# =============================================================================
macro(add_exe_module NAME)
    cmake_parse_arguments(ARG "" "OUTPUT_NAME" "DEPENDS;EXTRA_SOURCES" ${ARGN})

    if(ARG_OUTPUT_NAME)
        set(_MODULE_NAME ${ARG_OUTPUT_NAME})
    else()
        set(_MODULE_NAME ${NAME})
    endif()

    file(GLOB_RECURSE ${NAME}_SOURCES CONFIGURE_DEPENDS *.cpp)
    file(GLOB_RECURSE ${NAME}_HEADERS CONFIGURE_DEPENDS include/*.h include/*.hpp)

    add_executable(${NAME} ${${NAME}_SOURCES} ${${NAME}_HEADERS} ${ARG_EXTRA_SOURCES})
    target_include_directories(${NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
    _set_output_dirs(${NAME} "${CMAKE_BINARY_DIR}/${RUNTIME_OUTPUT_SUBDIR}")
    _apply_rpath(${NAME} "$ORIGIN")
    _register_module_target(PRIMARY ${NAME})
    _register_executable_target(${NAME})

    _finalize_module_target(${NAME} ${_MODULE_NAME} ${ARGN})
endmacro()

# =============================================================================
# add_test_module(NAME [OUTPUT_NAME name] [DEPENDS ...] [EXTRA_SOURCES ...] [NO_DOCS])
#
# Called from a test module's own CMakeLists.txt (e.g. source/tests/mytest/CMakeLists.txt).
# Lands in bin/tests/, so shared-lib deps one level up need $ORIGIN/...
# EXCLUDE_FROM_ALL keeps it out of the default build; BUILD_TESTS opts it
# back in.
# =============================================================================
macro(add_test_module NAME)
    cmake_parse_arguments(ARG "" "OUTPUT_NAME" "DEPENDS;EXTRA_SOURCES" ${ARGN})

    if(ARG_OUTPUT_NAME)
        set(_MODULE_NAME ${ARG_OUTPUT_NAME})
    else()
        set(_MODULE_NAME ${NAME})
    endif()

    file(GLOB_RECURSE ${NAME}_SOURCES CONFIGURE_DEPENDS *.cpp)
    file(GLOB_RECURSE ${NAME}_HEADERS CONFIGURE_DEPENDS include/*.h include/*.hpp)

    if(BUILD_TESTS)
        add_executable(${NAME} ${${NAME}_SOURCES} ${${NAME}_HEADERS} ${ARG_EXTRA_SOURCES})
    else()
        add_executable(${NAME} EXCLUDE_FROM_ALL ${${NAME}_SOURCES} ${${NAME}_HEADERS} ${ARG_EXTRA_SOURCES})
    endif()
    target_include_directories(${NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    _set_output_dirs(${NAME} "${CMAKE_BINARY_DIR}/${RUNTIME_OUTPUT_SUBDIR}/tests")
    _apply_rpath(${NAME} "$ORIGIN/..")
    _register_module_target(TEST ${NAME})
    _register_executable_target(${NAME})

    _finalize_module_target(${NAME} ${_MODULE_NAME} ${ARGN})

    add_test(NAME ${NAME} COMMAND ${NAME})
    add_dependencies(all_tests ${NAME})
endmacro()

# =============================================================================
# add_example_module(NAME [OUTPUT_NAME name] [DEPENDS ...] [EXTRA_SOURCES ...] [NO_DOCS])
#
# Called from an example module's own CMakeLists.txt (e.g. source/examples/myexample/CMakeLists.txt).
# Same bin/examples/ + $ORIGIN/.. layout as tests.
# =============================================================================
macro(add_example_module NAME)
    cmake_parse_arguments(ARG "" "OUTPUT_NAME" "DEPENDS;EXTRA_SOURCES" ${ARGN})

    if(ARG_OUTPUT_NAME)
        set(_MODULE_NAME ${ARG_OUTPUT_NAME})
    else()
        set(_MODULE_NAME ${NAME})
    endif()

    file(GLOB_RECURSE ${NAME}_SOURCES CONFIGURE_DEPENDS *.cpp)
    file(GLOB_RECURSE ${NAME}_HEADERS CONFIGURE_DEPENDS include/*.h include/*.hpp)

    if(BUILD_EXAMPLES)
        add_executable(${NAME} ${${NAME}_SOURCES} ${${NAME}_HEADERS} ${ARG_EXTRA_SOURCES})
    else()
        add_executable(${NAME} EXCLUDE_FROM_ALL ${${NAME}_SOURCES} ${${NAME}_HEADERS} ${ARG_EXTRA_SOURCES})
    endif()
    target_include_directories(${NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    _set_output_dirs(${NAME} "${CMAKE_BINARY_DIR}/${RUNTIME_OUTPUT_SUBDIR}/examples")
    _apply_rpath(${NAME} "$ORIGIN/..")
    _register_module_target(EXAMPLE ${NAME})
    _register_executable_target(${NAME})

    _finalize_module_target(${NAME} ${_MODULE_NAME} ${ARGN})

    add_dependencies(all_examples ${NAME})
endmacro()
