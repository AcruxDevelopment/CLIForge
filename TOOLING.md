# Tooling for the C/C++ Coding Style Guide

This repo ships five files that automate as much of [cpp-style-guide.md](cpp-style-guide.md) as tooling can reach:

| File | Enforces |
|---|---|
| [`.clang-format`](#clang-format) | All formatting: tabs, Allman braces, namespace indentation, pointer alignment, include order, line length |
| [`.clang-tidy`](#clang-tidy) | Naming conventions for Part I (C++), plus a subset of the Language Feature Do's and Don'ts |
| [`c-api/.clang-tidy`](#c-apiclang-tidy) | Naming conventions for Part II (C API) — `snake_case` instead of `camelCase`/`PascalCase` |
| [`.editorconfig`](#editorconfig) | Baseline tabs/whitespace for editors that don't run clang-format live |
| [`.gitattributes`](#gitattributes) | Forces LF line endings for source files on every OS, so diffs and formatting checks agree across Linux/Windows/Mac |
| [`.githooks/pre-commit`](#githookspre-commit) | Runs clang-format + clang-tidy automatically on every commit — pure bash, no Python or third-party framework, works on Linux/macOS/Windows |

Put all of these at your repo root except `c-api/.clang-tidy`, which goes at the root of whatever directory holds your Part II surface (rename the folder to match your actual layout — `c-api/`, `public-include/`, etc.).

---

## `.clang-format`

Covers [§2 Indentation](cpp-style-guide.md#2-indentation), [§3 Brace Placement](cpp-style-guide.md#3-brace-placement-allman-style), [§4 Pointers and References](cpp-style-guide.md#4-pointers-and-references), [§5 Spacing](cpp-style-guide.md#5-spacing), [§7 Include Directive Order](cpp-style-guide.md#7-include-directive-order), [§8 Line Length](cpp-style-guide.md#8-line-length), and [Namespaces](cpp-style-guide.md#namespaces) (indentation + no trailing brace comments), in full — running `clang-format -i` reformats a file to match all of these automatically.

One caveat: the include-order regexes classify a header by *how it's written*, not what it actually is — `<vector>` is correctly detected as C++ standard library, but a third-party header with no slash and no extension (rare, but it happens) could be miscategorized. Spot-check unusual includes rather than trusting the grouping blindly.

## `.clang-tidy`

Covers the naming table in [§1 Naming Conventions](cpp-style-guide.md#1-naming-conventions) for Part I via `readability-identifier-naming`, plus a handful of checks tied to specific rules in [§10 Language Feature Do's and Don'ts](cpp-style-guide.md#10-language-feature-dos-and-donts):

- `cppcoreguidelines-owning-memory` / `cppcoreguidelines-no-malloc` → [Memory and Ownership](cpp-style-guide.md#memory-and-ownership): flags raw owning pointers and C-style allocation.
- `modernize-use-nullptr`, `modernize-use-override`, `cppcoreguidelines-pro-type-cstyle-cast` → [Modern C++ Features](cpp-style-guide.md#modern-c-features).
- `modernize-concat-nested-namespaces` → [Namespaces](cpp-style-guide.md#namespaces): rewrites manually nested `namespace network_utils { namespace detail { ... } }` into the required `namespace network_utils::detail { ... }` form (`clang-tidy --fix` applies it automatically).
- `bugprone-exception-escape` → [Error Handling](cpp-style-guide.md#error-handling): flags exceptions escaping `noexcept` contexts, including destructors.
- `GlobalVariableCase`/`GlobalVariablePrefix` and `StaticVariableCase`/`StaticVariablePrefix` → [§1](cpp-style-guide.md#1-naming-conventions): `g_`/`s_` for mutable global and `static` variables. `ClassMemberCase`/`ClassMemberPrefix` covers the same `s_` rule for static class data members specifically, kept separate from the `m_`-prefixed instance members. Constants are unaffected — `GlobalConstantCase` still requires `PascalCase` with no prefix, since a `const`/`constexpr` global isn't the mutable state these prefixes exist to flag.

## `c-api/.clang-tidy`

A second, directory-scoped `.clang-tidy` that overrides naming to match [§13 Naming Conventions (C API)](cpp-style-guide.md#13-naming-conventions-c-api) — `snake_case` functions/variables, `UPPER_CASE` enum values and macros — without touching the rest of the project. clang-tidy always walks up from the file being checked to the nearest `.clang-tidy`, so this is the standard way to give one subtree different rules; no per-file flags needed. `InheritParentConfig: true` keeps the shared checks (like `bugprone-exception-escape`) from the root config active here too.

## `.editorconfig`

A fallback for editors/IDEs that display tabs-vs-spaces before clang-format has a chance to run on save. Doesn't replace `.clang-format` — just prevents a raw, unformatted first draft from using spaces.

## `.gitattributes`

Forces LF line endings for source files on checkout, regardless of each contributor's OS or local `core.autocrlf` setting. Without this, the exact same file can end up CRLF on a Windows checkout and LF on Linux/Mac — which makes `clang-format`'s output and the on-disk file disagree on every single line, not because of real formatting drift but purely because of line endings. This is what makes the `.githooks/pre-commit` diff check (and `clang-format` in general) behave identically across platforms.

## `.githooks/pre-commit`

A plain bash script — no Python, no `pip install`, no third-party framework — that runs `clang-format` and `clang-tidy` against staged C/C++ files before each commit.

**Setup (once per clone):**
```
chmod +x .githooks/pre-commit
git config core.hooksPath .githooks
```

**Tool availability is optional, by design:**
- If `clang-format` or `clang-tidy` isn't found on `PATH`, the hook prints a yellow warning and skips that check — it never blocks a commit just because a contributor hasn't installed the tools.
- If a tool *is* installed and finds real formatting or lint issues, the commit is blocked with a red error and the fix command (`clang-format -i ...`).
- `clang-tidy` additionally needs `compile_commands.json` to resolve `#include`s; if that's missing, it's treated the same as a missing binary — a warning, not a failure. Generate one with `cmake -DCMAKE_EXPORT_COMPILE_COMMANDS=ON` (or `bear -- make` for non-CMake builds).

Because `core.hooksPath` is a local git config, each contributor runs the setup command once — it isn't automatically enabled by cloning the repo. If you want it enforced repo-wide regardless of local setup, run the same checks in CI as a second, unconditional layer.

---

## Platform notes

The whole toolset — `.clang-format`, `.clang-tidy`, `c-api/.clang-tidy`, `.editorconfig`, `.gitattributes`, and `.githooks/pre-commit` — works unmodified on Linux, macOS, and Windows. A few specifics:

- **Linux:** install the tools via your distro's package manager, e.g. `apt install clang-format clang-tidy` (Debian/Ubuntu) or the equivalent for your distro.
- **macOS:** `brew install clang-format llvm` (Homebrew's `llvm` formula includes `clang-tidy`; it isn't symlinked onto `PATH` by default — `brew --prefix llvm` shows where it installed, add its `bin/` to `PATH`).
- **Windows:** install LLVM from [releases.llvm.org](https://releases.llvm.org) or `winget install LLVM.LLVM`, which puts `clang-format.exe`/`clang-tidy.exe` on `PATH`. The `.githooks/pre-commit` script itself needs no separate installation or WSL — it runs through Git for Windows' bundled Git Bash automatically, since `git.exe` reads the `#!/usr/bin/env bash` line at the top of the hook and executes it through that shell. If a contributor doesn't have the LLVM tools installed, the hook warns and skips, exactly as on Linux/Mac.
- **Line endings:** `.gitattributes` normalizes everything to LF on checkout regardless of platform or a contributor's `core.autocrlf` setting, so `clang-format`'s output always matches the on-disk file byte-for-byte and the hook's diff check doesn't produce false positives from CRLF/LF mismatches.

---

## What these tools can't check

Some rules in the guide are structural or naming-adjacent in ways `clang-format`/`clang-tidy` can't reach. These stay as code-review checklist items:

- **File naming** — [§1](cpp-style-guide.md#1-naming-conventions)'s `PascalCase` `.hpp`/`.cpp` file names and [§13](cpp-style-guide.md#13-naming-conventions-c-api)'s `snake_case` `.h`/`.c` file names. Neither tool renames or checks file names.
- **One type per header** — [§6](cpp-style-guide.md#6-header-files) and the one-template-per-header rule in [Templates](cpp-style-guide.md#templates). No standard clang-tidy check counts top-level declarations per file.
- **The `#include`/forward-declare choice and the Pimpl idiom** — [Headers as Public API](cpp-style-guide.md#headers-as-public-api-keep-implementation-types-out) is a design decision, not a formatting or naming rule.
- **The C API symbol-prefix convention** (e.g. `net_manager_*`) in [§13](cpp-style-guide.md#13-naming-conventions-c-api) — `c-api/.clang-tidy` enforces the *casing*, but a project-specific prefix isn't something a generic naming check can require without hardcoding your library name into the config.
- **Exceptions-for-true-exceptions-only** in [Error Handling](cpp-style-guide.md#error-handling) — whether a given `throw` represents a genuinely exceptional condition versus expected control flow is a judgment call no linter makes reliably.
- **The static/dynamic/runtime-load guidance** in [§11](cpp-style-guide.md#11-library-linking-and-runtime-loading) — these are architectural decisions about what crosses a library boundary, not something a formatter enforces per line.
