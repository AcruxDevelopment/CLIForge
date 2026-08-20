# CMake module-system demo

A minimal C++20 project showing a per-module CMake setup: each module owns
a tiny, self-contained `CMakeLists.txt`, the root `CMakeLists.txt`
discovers modules automatically, three ways to bring in third-party code
(vendored, installed, or fetched) stay out of your warnings and your
compile database, every module gets its own browsable documentation
automatically the moment Doxygen is installed, and every output path is
configured in one place.

## Layout

```
CMakeLists.txt                 project setup, options, install(), discovery -- edit by hand
Configuration.cmake             all output-path configuration -- edit to relocate anything
cmake/BuildHelpers.cmake        module macros + discovery mechanism -- edit rarely
cmake/Dependencies.cmake        vendoring / installed-library / FetchContent helpers -- edit rarely
cmake/Documentation.cmake       automatic per-module Doxygen integration -- edit rarely
source/Core/CMakeLists.txt      library module "core_lib" (SHARED), depends on fmt
source/HelloApp/CMakeLists.txt  executable module "hello_exe", depends on vendored tiny_ansi
source/FarewellApp/CMakeLists.txt  executable module "farewell_exe", depends on vendored tiny_case
vendor/tiny_ansi/               header-only vendored dependency (no build of its own)
vendor/tiny_case/               vendored dependency with its own CMakeLists.txt
scripts/build.sh, build.bat     configure + build
scripts/run.sh, run.bat         run a module by its CMake target name
scripts/install.sh, install.bat install this project's own artifacts only
scripts/docs.sh, docs.bat       build documentation for every module (or one)
.clang-format, .clang-tidy      C++ style guide tooling (see "Code style & tooling" below)
.editorconfig, .gitattributes   -- same
c-api/.clang-tidy               naming override template for a C API subtree, if you add one
.githooks/pre-commit            formatting/lint pre-commit hook
cpp-style-guide.md, TOOLING.md  the style guide itself + tooling docs
```

Everything generated -- the build tree, install output, docs, and reserved
packaging output -- lives under one `out/` directory (see "Output paths"
below); nothing under `out/` is source-controlled.

Each module's `CMakeLists.txt` is (conventionally) a couple of lines:

```cmake
# source/Core/CMakeLists.txt
add_dependency(fmt
    GIT_REPOSITORY https://github.com/fmtlib/fmt.git
    GIT_TAG        11.0.2
    FIND_PACKAGE_ARGS NAMES fmt
)
add_lib_module(core_lib TYPE SHARED OUTPUT_NAME "demo_core" DEPENDS fmt::fmt)
```

A module declares its own dependencies in its own `CMakeLists.txt` -- same
"minimal tool intervention" principle as module registration: nothing to
add in the root file.

## Output paths (`Configuration.cmake`)

Every generated path is a cache variable, overridable with `-D<NAME>=...`
at configure time instead of editing any file:

| Variable | Default | What it controls |
|---|---|---|
| `OUT_DIR` | `out/` | Root of everything generated; override this to relocate all of the below at once |
| `INSTALL_OUTPUT_DIR` | `out/install` | Root of the `cmake --install` tree, one subdirectory per `DIST_TARGET` |
| `DIST_OUTPUT_DIR` | `out/dist` | Reserved for packaged/archived output (e.g. a future CPack integration) -- not populated by this project yet, but created and ready |
| `DOCS_OUTPUT_DIR` | `out/docs` | Root of generated Doxygen documentation, one subdirectory per module |
| `TOOLCACHE_DIR` | `.cache/tools` | Where downloaded, pinned build tools (currently just Doxygen) are cached -- kept outside `OUT_DIR` so `rm -rf out/` doesn't force a re-download |
| `RUNTIME_OUTPUT_SUBDIR` | `bin` | Subdirectory (inside whatever the build dir is) for executables/`.dll`/`.so` |
| `LIBRARY_OUTPUT_SUBDIR` | `bin` | Subdirectory for shared-library artifacts (kept alongside executables so `$ORIGIN` RPATH resolves them) |
| `ARCHIVE_OUTPUT_SUBDIR` | `lib` | Subdirectory for static/import-library artifacts |

The build directory itself (`CMAKE_BINARY_DIR`, where `CMakeCache.txt`
lives) isn't a `Configuration.cmake` variable -- it's fixed by whatever you
pass to `-B` when configuring. `scripts/build.sh`/`build.bat` default to
`out/build`, so the resulting layout is:

```
out/
  build/                          CMAKE_BINARY_DIR -- out/build/bin, out/build/lib during normal builds
  install/<DIST_TARGET>/{bin,lib} what `cmake --install` / scripts/install.sh populates
  dist/                           reserved for future packaging output
  docs/<module>/html/, docs/index.html   generated documentation
```

Example -- build into a different location entirely:

```bash
cmake -G Ninja -S . -B /tmp/my-build -DOUT_DIR=/tmp/my-out
cmake --build /tmp/my-build
```

## Three ways to bring in a dependency (`cmake/Dependencies.cmake`)

| Helper | For | Example |
|---|---|---|
| `add_vendor_header_only(NAME [INCLUDE_DIR <dir>])` | A buildless header-only drop under `vendor/<NAME>/include` | `vendor/tiny_ansi/` |
| `add_vendor_subdirectory(SUBDIR)` | A vendored dependency with its own `CMakeLists.txt`, under `vendor/<SUBDIR>` | `vendor/tiny_case/` |
| `add_dependency(NAME <FetchContent_Declare args>)` | An installed package (`FIND_PACKAGE_ARGS ...`) with a fetch-from-source fallback | `fmt` in `source/Core/` |

All three are safe to call more than once for the same name (e.g. two
modules that need the same dependency) and mark the dependency's headers
`SYSTEM`, so `-Wall -Wextra -Wpedantic`/`/W4` -- which we only ever apply
per-target inside our own module macros, never globally -- doesn't fire on
code you don't control. Their targets are also dropped from
`compile_commands.json` (see below).

`add_dependency` is CMake's native find-then-fetch integration: it tries
`find_package()` first (an already-installed apt/vcpkg/conan copy) via
`FIND_PACKAGE_ARGS`, and only downloads + builds from source if nothing is
found. Omit `FIND_PACKAGE_ARGS` to always fetch. Requires network access
the first time a dependency isn't already installed.

A module `DEPENDS` on whatever target the chosen path defines (`fmt::fmt`,
`tiny_case`, `tiny_ansi`, ...) exactly like depending on another module --
`add_lib_module`/`add_exe_module` don't need to know or care which of the
three supplied it.

## Documentation (automatic, per module, pinned Doxygen)

Every module registered via `add_lib_module`/`add_exe_module`/
`add_test_module`/`add_example_module` gets its own
[Doxygen](https://www.doxygen.nl/) documentation target automatically --
nothing to add to a module's own `CMakeLists.txt`, and no Doxygen install
required on your machine. This project downloads and manages its own
pinned Doxygen (currently **1.16.1**, see `DOXYGEN_PINNED_VERSION` in
`cmake/Documentation.cmake`) the first time you configure with
`ENABLE_DOXYGEN` on (the default) -- it deliberately never looks for or
uses a system-installed `doxygen`, so every machine building this project
generates identical output regardless of what (if anything) is on `PATH`.
The download is cached under `.cache/tools/` (`TOOLCACHE_DIR`, see
"Output paths" above), so it only happens once per machine -- a clean
`rm -rf out/` does not force a re-download.

```bash
scripts/docs.sh                # build docs for every module, prints where to open it
scripts/docs.sh core_lib        # build docs for just one module (docs_<target>)
```
```bat
scripts\docs.bat
scripts\docs.bat core_lib
```

Output goes to `out/docs/<module>/html/index.html` (a sibling of
`out/build/`, not nested inside it -- see "Output paths" above), plus a
landing page at `out/docs/index.html` linking to every module.
Documentation is **not** built as part of a normal `scripts/build.sh` --
it's opt-in, the same way tests and examples are, since generating it on
every compile would be wasteful. If the pinned Doxygen can't be downloaded
(no network access, an unsupported host platform, or you passed
`-DENABLE_DOXYGEN=OFF`), `scripts/docs.sh` prints a clear message instead
of failing -- everything else keeps building normally either way:

```
Documentation is disabled (ENABLE_DOXYGEN=OFF, or Doxygen isn't installed). Install Doxygen and reconfigure to enable it.
```

**Writing docs for a module** is just normal Doxygen comments in that
module's headers -- e.g. `source/Core/include/core/Greeting.hpp`:

```cpp
/// Builds a greeting for @p name.
///
/// @param name  the person (or thing) being greeted.
/// @return a formatted greeting string, e.g. "Hello, Ale! (from demo_core)".
std::string makeGreeting(const std::string& name);
```

Even with zero comments, a module's docs are still generated and useful --
file lists, function/class signatures, and (if [Graphviz](https://graphviz.org/)
is installed on your system -- this one enhancement, unlike Doxygen
itself, is still detected rather than pinned, since it's optional) call
graphs are produced regardless, since `DOXYGEN_EXTRACT_ALL` is on by
default. Comments just make it richer. Drop a `README.md` in a module's
own directory and it's automatically used as that module's Doxygen main
page.

**Configuring it** -- nothing is required, but everything is overridable:

```bash
# Disable entirely, even though Doxygen would otherwise download fine:
cmake -S . -B out/build -DENABLE_DOXYGEN=OFF

# Pin a different Doxygen version (also update the two SHA256 hashes in
# cmake/Documentation.cmake to match that release's assets -- a stale
# hash fails the configure loudly, on purpose, rather than silently
# accepting an unexpected file):
cmake -S . -B out/build -DDOXYGEN_PINNED_VERSION=1.17.0

# Any Doxyfile tag can be set as a CMake variable, e.g. stricter output:
cmake -S . -B out/build -DDOXYGEN_WARN_IF_UNDOCUMENTED=YES -DDOXYGEN_QUIET=NO
```

A single module can opt out with `NO_DOCS`:

```cmake
# some/internal/module/CMakeLists.txt
add_lib_module(internal_only TYPE STATIC NO_DOCS)
```

Vendored and fetched dependencies (`cmake/Dependencies.cmake`) are never
auto-documented -- same reasoning as their `compile_commands.json`
exclusion, it isn't your code.

## compile_commands.json / clang tooling

`CMAKE_EXPORT_COMPILE_COMMANDS` is on by default, and every build copies
`compile_commands.json` from the build directory to the project root so
clangd/clang-tidy/IWYU find it without extra configuration (CMake only
ever writes it into the build directory; most editors look for it at the
repo root). Vendored and fetched dependencies are excluded from it, so it
only reflects code this project actually owns. The project also builds
cleanly with clang directly:

```bash
cmake -G Ninja -S . -B out/build -DCMAKE_CXX_COMPILER=clang++ -DDIST_TARGET=linux-x64-clang
cmake --build out/build
```

## Code style & tooling

This project follows [`cpp-style-guide.md`](cpp-style-guide.md) (tabs,
Allman braces, `PascalCase` file names, `camelCase` functions/variables,
`m_`/`s_`/`g_` member/static/global prefixes -- see the file itself for
the full rules and rationale), enforced by the tooling described in
[`TOOLING.md`](TOOLING.md):

| File | Enforces |
|---|---|
| `.clang-format` | All formatting: tabs, Allman braces, pointer alignment, include order, line length |
| `.clang-tidy` | Naming conventions and a subset of language-feature rules, for this project's own C++ code |
| `c-api/.clang-tidy` | A template naming override (`snake_case`) for a C API subtree, if/when this project has one -- see below |
| `.editorconfig` | Baseline tabs/whitespace for editors that don't run clang-format live |
| `.gitattributes` | Forces LF line endings for source files on every OS |
| `.githooks/pre-commit` | Runs clang-format + clang-tidy on staged files before each commit; warns and skips (never blocks) if a tool isn't installed |

**One-time setup per clone** to activate the hook:

```bash
chmod +x .githooks/pre-commit
git config core.hooksPath .githooks
```

**clang-tidy needs `compile_commands.json`** to resolve `#include`s --
this project already generates and copies it to the repo root on every
build (see above), so as long as you've built at least once, the hook's
clang-tidy check works with no extra setup.

Two adjustments made when integrating the toolkit into this specific
project (not part of the toolkit as provided -- see `TOOLING.md` in this
repo for the toolkit's own documentation of everything else):

- **`vendor/` is excluded** from the pre-commit hook's staged-file check
  (`git diff ... ':!vendor/*'`). Vendored code (`cmake/Dependencies.cmake`)
  isn't this project's code, so it isn't held to this style guide -- same
  reasoning as its `compile_commands.json` and warning-flag exclusions
  elsewhere in this project.
- **`.clang-tidy` gained one option**, `LocalConstantCase: camelBack`. The
  style guide's own text says only *global/static* `const`/`constexpr`
  values are "constants" for naming purposes -- everything else stays
  under the `camelCase` Local Variables rule. Without this option, a plain
  local `const` (e.g. `const std::string name = ...;` inside a function)
  silently fell through to the general `PascalCase` constant rule instead,
  which would have flagged completely ordinary code. Verified against a
  small isolated test file: with the fix, a local `const` is accepted in
  `camelCase` while a `static const`/`constexpr` still correctly requires
  `PascalCase`.

`c-api/.clang-tidy` is included as a template, positioned exactly as the
toolkit ships it (`c-api/.clang-tidy`) -- this project doesn't currently
have a C API (Part II) surface. If you add one, move this file to that
subtree's root (`c-api/`, `public-include/`, wherever your `extern "C"`
headers live); `clang-tidy` always uses the closest `.clang-tidy` file up
the directory tree, so files under that directory automatically pick up
the C API's `snake_case` naming instead of this project's `camelCase`.

This project's own code was reformatted and renamed to comply when the
toolkit was integrated -- e.g. `Greeting.hpp`/`.cpp` (was `greeting.h`/
`.cpp`) and `makeGreeting`/`makeFarewell` (was `make_greeting`/
`make_farewell`) in `source/Core/`. `main.cpp` in each app module became
`Main.cpp` for the same reason (`PascalCase` file names) -- the style
guide doesn't call out an entry-point exception, so none was assumed;
rename back to lowercase if you'd rather treat that specific convention
as an exception.

## How module discovery works

The root `CMakeLists.txt` calls `_discover_module_subdirectories()` on
three roots, in this order:

| Root | What it holds |
|---|---|
| `source/*` | library AND app/executable modules, side by side |
| `source/tests/*` | test modules |
| `source/examples/*` | example modules |

(`source/tests` and `source/examples` aren't picked up by the first call
-- they have no `CMakeLists.txt` directly inside them, only their own
children do.)

Any immediate child directory that contains its own `CMakeLists.txt` gets
`add_subdirectory()`'d automatically (`vendor/` is not one of these roots
-- vendored dependencies are opted into explicitly by the modules that
need them, not auto-built by default). This uses `CONFIGURE_DEPENDS`, so
the *next build* reconfigures on its own after you add or remove a module
directory -- no manual `cmake` re-run needed. A module may `DEPENDS` on a
module discovered later in this list; order doesn't affect linking (CMake
resolves target names across the whole project at generate time --
verified, not assumed).

## Requirements

CMake >= 3.25, Ninja, a C++20 compiler (GCC/Clang/MSVC), `git` (for
FetchContent's `GIT_REPOSITORY`-based dependencies like `fmt`), and
network access the first time you configure (also needed for the pinned
Doxygen download -- both are cached afterward, see "Output paths" and
"Documentation" above). Nothing else to install: Doxygen is downloaded
and pinned automatically, not a system dependency.
[Graphviz](https://graphviz.org/) is optional for Doxygen call graphs, and
[clang-format](https://clang.llvm.org/docs/ClangFormat.html)/
[clang-tidy](https://clang.llvm.org/extra/clang-tidy/) are optional for
the pre-commit hook (see "Code style & tooling" above) -- both are
detected, not pinned, since they're editor/workflow tools rather than
part of the build itself.

## Build

```bash
scripts/build.sh            # Release (default)
scripts/build.sh Debug       # or Debug / RelWithDebInfo / MinSizeRel
```
```bat
scripts\build.bat
scripts\build.bat Debug
```

## Run

```bash
scripts/run.sh hello_exe
scripts/run.sh farewell_exe -- Ale
scripts/run.sh              # no args: lists available targets
```
```bat
scripts\run.bat hello_exe
scripts\run.bat farewell_exe -- Ale
```

`run.sh`/`run.bat` resolve the CMake **target id** (not the binary's
`OUTPUT_NAME`) to the actual binary via a manifest CMake generates at
configure time, so these scripts never need editing when a module is
added, removed, or renamed.

## Install

```bash
scripts/install.sh
```
```bat
scripts\install.bat
```

Always use the install script (or pass `--component demo` yourself if
calling `cmake --install` directly). A vendored/fetched dependency's own
`CMakeLists.txt` usually has `install()` rules of its own with no
component tag; installing without `--component` would also run those,
typically dumping files under `CMAKE_INSTALL_PREFIX` (e.g. `/usr/local`).
Tagging this project's own `install(TARGETS ...)` calls with `COMPONENT
demo` and always filtering on it is what keeps `cmake --install` scoped to
just this project's own artifacts.

## Adding a module

1. Create a directory under the right root (see the discovery table above).
2. Add a `CMakeLists.txt` inside it with one `add_lib_module` /
   `add_exe_module` / `add_test_module` / `add_example_module` call, plus
   any `add_vendor_*`/`add_dependency` calls it needs.
3. Add your `.cpp`/`.h` files inside that same directory.
4. Rebuild (`scripts/build.sh`) -- no other file needs touching.

```cmake
# source/MyTool/CMakeLists.txt
add_exe_module(my_tool_exe OUTPUT_NAME "my-tool" DEPENDS core_lib)
```

`scripts/run.sh my_tool_exe` picks it up automatically once it's built,
and `scripts/docs.sh my_tool_exe` documents it automatically too -- both
with zero edits anywhere outside `source/MyTool/`.
