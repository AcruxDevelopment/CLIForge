# C/C++ Coding Style Guide

This guide is split into two parts:

- **[Part I: C++ Style](#part-i-c-style)** applies to all internal application/library code written in C++.
- **[Part II: C API Style](#part-ii-c-api-style)** applies specifically to public API surfaces exposed as plain C (`extern "C"`, `.h` headers) — e.g. for dynamic library boundaries, plugin interfaces, or C-consumer compatibility. Keep these two worlds separate: don't let C-API constraints (no exceptions, no classes, `snake_case`) leak into internal C++ code, and don't let C++ constructs leak into a public C API.

## Contents

**[Part I: C++ Style](#part-i-c-style)**
1. [Naming Conventions](#1-naming-conventions)
2. [Indentation](#2-indentation)
3. [Brace Placement (Allman Style)](#3-brace-placement-allman-style)
4. [Pointers and References](#4-pointers-and-references)
5. [Spacing](#5-spacing)
6. [Header Files](#6-header-files)
   - [Namespaces](#namespaces)
   - [Headers as Public API: Keep Implementation Types Out](#headers-as-public-api-keep-implementation-types-out)
   - [Templates](#templates)
     - [Implementing Templates (Project Standard)](#implementing-templates-project-standard)
7. [Include Directive Order](#7-include-directive-order)
8. [Line Length](#8-line-length)
9. [Comments](#9-comments)
10. [Language Feature Do's and Don'ts](#10-language-feature-dos-and-donts)
    - [Memory and Ownership](#memory-and-ownership)
    - [Error Handling](#error-handling)
    - [Modern C++ Features](#modern-c-features)
11. [Library Linking and Runtime Loading](#11-library-linking-and-runtime-loading)
    - [Static Linking](#static-linking)
    - [Dynamic Linking (Shared Libraries)](#dynamic-linking-shared-libraries)
    - [Runtime Loading (dlopen and LoadLibrary)](#runtime-loading-dlopen-and-loadlibrary)
    - [Summary](#summary)
12. [Miscellaneous](#12-miscellaneous)

**[Part II: C API Style](#part-ii-c-api-style)**
13. [Naming Conventions (C API)](#13-naming-conventions-c-api)
14. [File Extensions and Structure](#14-file-extensions-and-structure)
15. [Indentation and Braces](#15-indentation-and-braces)
16. [API Design Do's and Don'ts](#16-api-design-dos-and-donts)
17. [Boundary Rule of Thumb](#17-boundary-rule-of-thumb)

---

# Part I: C++ Style

## 1. Naming Conventions

| Symbol Type            | Convention        | Example                          |
|-------------------------|-------------------|-----------------------------------|
| Classes / Structs        | `PascalCase`       | `class NetworkManager;`          |
| Functions / Methods       | `camelCase`        | `void connectToServer();`         |
| Local Variables            | `camelCase`         | `int retryCount = 0;`              |
| Member Variables (private/protected) | `camelCase` with `m_` prefix | `int m_connectionTimeout;` |
| Static Member Variables (class-scope `static`) | `camelCase` with `s_` prefix | `static int s_instanceCount;` |
| Global Variables (namespace/file scope, mutable) | `camelCase` with `g_` prefix | `int g_activeConnectionCount;` |
| Static Variables (function-local or file-scope `static`) | `camelCase` with `s_` prefix | `static int s_retryAttempts;` |
| Constants                | `PascalCase`        | `constexpr int MaxRetries = 5;`   |
| Enum Type Names            | `PascalCase`        | `enum class ConnectionState`      |
| Enum Values                 | `PascalCase`         | `ConnectionState::Connected`       |
| Namespaces                | `snake_case`        | `namespace network_utils`          |
| Template Parameters       | `PascalCase`         | `template <typename ValueType>`   |
| Macros                     | `ALL_CAPS_SNAKE_CASE` | `#define MAX_BUFFER_SIZE 1024`   |
| File Names                 | `PascalCase`         | `NetworkManager.cpp` / `.hpp`     |

> **Note:** Macros are an exception to the general PascalCase/camelCase scheme since `ALL_CAPS` is the near-universal C/C++ convention and helps macros stand out visually from regular code.

> **Note:** `g_`/`s_` mark *mutable* global/static state specifically so it stands out at every use site — genuinely global mutable variables are rare and worth flagging by name. A `constexpr` or `const` global/static value is a **constant**, not a variable for this rule's purposes: it keeps the `PascalCase` constant convention above, with no `g_`/`s_` prefix, since it can't cause the aliasing/lifetime bugs the prefix exists to call out (e.g. `constexpr int MaxRetries = 5;` at namespace scope, not `g_MaxRetries`). A `static` variable inside a function keeps `s_` for the same reason as a file-scope one — both persist across calls and are exactly the kind of hidden state the prefix is meant to surface.

---

## 2. Indentation

- Use **tabs** for indentation (not spaces).
- Do not mix tabs and spaces.
- Configure your editor's tab width to display consistently (recommended: 4 columns wide) across the team.
- **Namespace bodies are indented**, exactly like any other block (`if`, `class`, `for`, ...) — a namespace is not a special case left flush against the margin. See [Namespaces](#namespaces) for the full rule and examples.

```cpp
void doSomething()
{
	if (condition)
	{
		performAction();
	}
}
```

---

## 3. Brace Placement (Allman Style)

Braces always go on their **own new line**, aligned with the statement that opens the block.

```cpp
// Correct (Allman)
if (isReady)
{
	start();
}
else
{
	waitForReady();
}

class Widget
{
public:
	Widget();
	~Widget();
};

void process()
{
	for (int i = 0; i < count; ++i)
	{
		handle(i);
	}
}
```

```cpp
// Incorrect (K&R style — do not use)
if (isReady) {
	start();
}
```

---

## 4. Pointers and References

Attach `*` and `&` to the **type**, not the variable name.

```cpp
int* pointerToValue;
const std::string& nameRef;

void processData(const Data* data, int& outResult);
```

---

## 5. Spacing

- One space around binary operators: `a + b`, `x == y`.
- No space between a function name and its parentheses: `doWork()`, not `doWork ()`.
- One space after control-flow keywords: `if (`, `for (`, `while (`.

```cpp
if (a == b)
{
	total = a + b * c;
}
```

---

## 6. Header Files

- Use `#pragma once` at the top of every header.
- **`.hpp` for C++ headers, `.h` for C headers.** Any header containing C++-only constructs (classes, templates, namespaces, overloads, `enum class`, etc.) uses `.hpp`. Reserve `.h` for headers meant to be valid C — see [Part II: C API Style](#part-ii-c-api-style) for the public C API convention.
- **One `.cpp` per `.hpp`.** Each header declaring a class or module has exactly one corresponding source file implementing it, sharing the same base name (`NetworkManager.hpp` ↔ `NetworkManager.cpp`). Avoid dumping multiple unrelated classes' implementations into one `.cpp`, and avoid splitting one class's implementation across multiple `.cpp` files.
- **One class, struct, or enum per header — meaning one *publicly usable* type.** Each `.hpp` exposes exactly one type as part of its public API: the primary class/struct/enum a caller `#include`s the header to use. A header should never declare two independent, separately-usable types (e.g. `NetworkManager` and an unrelated `RetryPolicy`) side by side — each gets its own `.hpp`. This is what prevents the dependency problem: a file that only needs `RetryPolicy` should never be forced to also compile/depend on everything `NetworkManager` pulls in. Small types that have no meaning or use *outside* the primary type — a nested `enum class` used only as one of its parameters, a tag type only passed to its constructor — aren't "separate" types for this rule; they're part of the primary type's interface and stay in the same header.
- Header-only components (templates, small inline utilities) are the exception to the one-`.cpp`-per-`.hpp` rule — they may have a `.hpp` with no matching `.cpp` — but the one-type-per-header rule still applies to them (see [Templates](#templates) below).
- Match header/source file names to the primary class (`PascalCase`), e.g. `NetworkManager.hpp` / `NetworkManager.cpp` for `class NetworkManager`.
- **Do not add a trailing comment on a closing brace** (`}; // class NetworkManager`, `} // namespace network_utils`, `} // if (isReady)`, etc.), for namespaces, classes, functions, or any other block. Keep blocks short and readable enough that the comment isn't needed — if a block is too long to tell what its closing brace belongs to at a glance, the fix is to shorten or restructure the block, not to annotate the brace.

```cpp
// NetworkManager.hpp
#pragma once

namespace network_utils
{
	class NetworkManager
	{
	public:
		NetworkManager();
		~NetworkManager();

		void connectToServer();

	private:
		int m_connectionTimeout;
	};
}
```

### Namespaces

- **Namespace contents are indented**, exactly like the body of any other block. Don't leave everything inside a namespace flush against the left margin just because the namespace itself doesn't "do" anything at runtime — indentation is what makes nesting depth obvious at a glance, and a namespace is nesting.
- **Use the C++17 nested-namespace syntax (`namespace outer::inner { ... }`) instead of manually nesting namespace blocks**, whenever you're declaring a fixed, known multi-level namespace (e.g. `network_utils::detail`). Manually nesting `namespace network_utils { namespace detail { ... } }` for a case like this just adds an extra level of indentation and an extra closing brace for no benefit.
- The one case that still requires manual nesting: wrapping an **anonymous** namespace inside a named one (e.g. hiding a `.cpp`-local helper type inside `network_utils`) — anonymous namespaces have no name, so there's nothing to join with `::`.

```cpp
// Preferred — a fixed multi-level namespace uses the single nested-namespace form
namespace network_utils::detail
{
	struct RetryState
	{
		int attemptCount;
		int backoffMs;
	};
}
```

```cpp
// Avoid — manually nesting two named namespaces that could be joined with ::
namespace network_utils
{
	namespace detail
	{
		struct RetryState
		{
			int attemptCount;
			int backoffMs;
		};
	}
}
```

### Headers as Public API: Keep Implementation Types Out

Treat every header as a **public interface contract**, not a scratchpad for whatever the implementation happens to need. A caller including `NetworkManager.hpp` should see only what they need to *use* `NetworkManager` — not the types that exist purely to help `NetworkManager.cpp` implement it internally.

- **Implementation-only helper types belong in the `.cpp`, not the `.hpp`.** If a class needs a small helper struct, enum, or class that callers never see or interact with (a parser's internal token type, a lookup-table entry, a strategy object used only inside one function), define it in the `.cpp` file, wrapped in an unnamed (anonymous) namespace so it has internal linkage and can't leak or collide across translation units:

```cpp
// NetworkManager.cpp
#include "NetworkManager.hpp"

namespace network_utils
{
	namespace
	{
		// Implementation detail — never appears in the header, never used outside this file.
		struct RetryState
		{
			int attemptCount;
			int backoffMs;
		};
	}

	void NetworkManager::connectToServer()
	{
		RetryState state{0, 100};
		// ...
	}
}
```

- **Forward-declare instead of including, whenever the header only needs a pointer or reference to another type.** This keeps the header's dependency footprint to exactly the API surface it exposes, rather than transitively dragging in every header its implementation happens to use.

```cpp
// NetworkManager.hpp
#pragma once

namespace network_utils
{
	class Socket; // forward declaration — full definition only needed in NetworkManager.cpp

	class NetworkManager
	{
	public:
		void connectToServer();

	private:
		Socket* m_socket;
	};
}
```

- **One real C++ constraint to know:** a class's *own* private member variables (as opposed to helper types) generally must still be declared in the header, because C++ needs the complete type — including all members — to know the class's size and layout wherever it's used by value, on the stack, or as a direct member of another class. This is different from hiding a *helper type*; you're not hiding the fact that `NetworkManager` has a timeout, only hiding the definition of an unrelated struct it happens to use internally.
- If you need to hide private *members* too — for a stable ABI across a shared-library boundary (see [Library Linking and Runtime Loading](#11-library-linking-and-runtime-loading)) or to shrink recompiles when private internals change — the standard technique is the **Pimpl idiom** (pointer to implementation): the header holds only a forward-declared pointer, and the real member variables live in a struct defined entirely in the `.cpp`.

```cpp
// NetworkManager.hpp
#pragma once
#include <memory>

namespace network_utils
{
	class NetworkManager
	{
	public:
		NetworkManager();
		~NetworkManager();

		void connectToServer();

	private:
		struct Impl;               // forward-declared, defined only in NetworkManager.cpp
		std::unique_ptr<Impl> m_impl;
	};
}
```

```cpp
// NetworkManager.cpp
#include "NetworkManager.hpp"

namespace network_utils
{
	struct NetworkManager::Impl
	{
		int connectionTimeout;
	};

	NetworkManager::NetworkManager() : m_impl(std::make_unique<Impl>()) {}
	NetworkManager::~NetworkManager() = default;

	void NetworkManager::connectToServer()
	{
		// use m_impl->connectionTimeout
	}
}
```

  Pimpl isn't required for every class — it adds an extra pointer indirection and a heap allocation, so reserve it for types where header-hiding genuinely matters (public library API, ABI-sensitive dynamic-library boundaries, or classes whose private internals churn often enough that reducing rebuild fan-out is worth the cost). For ordinary internal application classes, plain private members in the header (as in the example at the top of this section) are fine — only the *helper types*, not the members themselves, need to move out.

### Templates

Templates are *more* exposed to the one-type-per-header problem than ordinary classes, for two structural reasons specific to how templates compile:

1. **No compiled-`.cpp` firewall.** An ordinary class hides its implementation behind a `.cpp` — callers only need the declaration to link against it. A template has no such firewall: the compiler needs the *full definition* everywhere it's instantiated, so templates live entirely in headers (see the exception to "one `.cpp` per `.hpp`" in [Header Files](#6-header-files)). That means anything sharing a header with a template gets pulled into every translation unit that only wanted that one template.
2. **Parsing cost applies even to unused templates.** Bundling `template <typename T> class Cache` and `template <typename T> class Pool` in the same header means a file that only uses `Cache` still has to parse `Pool`'s full definition, and a change to `Pool` still bumps the header's timestamp — forcing a rebuild of every translation unit that includes it, including the ones that never touch `Pool` at all.

Given that, the rules:

- **One template class/function per header.** `Cache` and `Pool` each get their own `.hpp`, even if they're conceptually related utilities that usually get used together.
- **Keep a template's declaration and inline implementation in the same `.hpp`** — templates generally can't be split into a separate `.cpp` the way non-template code can (see reason 1 above) — but that's not license to merge *other*, unrelated templates into the same file alongside it.
- **If several templates genuinely share implementation details** (a common traits struct, a shared constant), factor *only that shared piece* into its own single-purpose header, rather than merging the templates themselves into one file. If the shared piece belongs in a nested namespace (e.g. an internal `detail` namespace not meant for outside use), declare it with the `::` nested-namespace syntax from [Namespaces](#namespaces).
- **Avoid including heavy headers** (e.g. `<iostream>`, large third-party headers) inside a widely-used template header purely for convenience — because template headers are included wherever they're instantiated, an unnecessary include here silently bloats compile times project-wide, multiplied across every instantiation site.

```cpp
// Avoid — two unrelated templates sharing one header
// Cache.hpp
#pragma once

namespace storage
{
	template <typename Key, typename Value>
	class Cache
	{
	public:
		void insert(const Key& key, const Value& value);
		const Value* find(const Key& key) const;
	};

	template <typename T>
	class Pool
	{
	public:
		T* acquire();
		void release(T* item);
	};
}
```

```cpp
// Preferred — one template per header
// Cache.hpp
#pragma once

namespace storage
{
	template <typename Key, typename Value>
	class Cache
	{
	public:
		void insert(const Key& key, const Value& value);
		const Value* find(const Key& key) const;
	};
}
```

```cpp
// Pool.hpp — separate file, included only where Pool is actually needed
#pragma once

namespace storage
{
	template <typename T>
	class Pool
	{
	public:
		T* acquire();
		void release(T* item);
	};
}
```

If `Cache` and `Pool` both rely on the same internal helper — say, a fixed-capacity ring buffer trait — that shared piece moves to its own header, in an internal namespace declared with `::`:

```cpp
// DetailTraits.hpp — included by both Cache.hpp and Pool.hpp, nothing else
#pragma once

namespace storage::detail
{
	template <typename T, std::size_t Capacity>
	struct RingBufferTraits
	{
		using ValueType = T;
		static constexpr std::size_t capacity = Capacity;
	};
}
```

#### Implementing Templates (Project Standard)

Templates are the one exception to this guide's header-declares/`.cpp`-implements split: the compiler needs a template's *complete definition* at every point it gets instantiated, and a separately-compiled `.cpp` can't supply that. So the implementation has to live in the header — but to keep this consistent across the codebase rather than a per-template judgment call, **this project uses one standard pattern**:

> **Declare template members in the class body, and define them out-of-line further down in the same header** — the same shape as an out-of-line member definition of a non-template class, just kept in the `.hpp` instead of moved to a `.cpp`.

```cpp
// Cache.hpp
#pragma once

namespace storage
{
	template <typename Key, typename Value>
	class Cache
	{
	public:
		void insert(const Key& key, const Value& value);
		const Value* find(const Key& key) const;

	private:
		std::unordered_map<Key, Value> m_entries;
	};

	template <typename Key, typename Value>
	void Cache<Key, Value>::insert(const Key& key, const Value& value)
	{
		m_entries[key] = value;
	}

	template <typename Key, typename Value>
	const Value* Cache<Key, Value>::find(const Key& key) const
	{
		auto it = m_entries.find(key);
		return it != m_entries.end() ? &it->second : nullptr;
	}
}
```

- **Exception:** a genuinely trivial one-line body (a plain getter, a one-expression forwarding call) can stay inline in the class, same as it would for a non-template class. Don't force it out-of-line just for consistency's sake.
- **Escape hatch, not a pattern:** if a template is only ever meant to be used with a small, fixed set of types, its definitions *can* move to a `.cpp` via explicit instantiation (`template class Cache<int, std::string>;`). This is a deliberate, rare trade-off — it turns a general-purpose template into one that only supports the explicitly instantiated types — and should be a documented exception with a stated reason (e.g. reducing header bloat for a widely-included, ABI-sensitive type), not the default way templates get written.

> **See also:** [Library Linking and Runtime Loading](#11-library-linking-and-runtime-loading) — the Pimpl idiom above exists mainly to satisfy the ABI constraints described there · [Part II: C API Style](#part-ii-c-api-style) — the `.h`/`.hpp` split introduced here is defined in full there.

---

## 7. Include Directive Order

Group includes into blocks, separated by a blank line, in the following order. Within each block, sort alphabetically.

1. **Corresponding header** — for a `.cpp`, its own `.hpp` comes first (catches missing-include bugs in the header itself).
2. **C system headers** — `<cstdio>`, `<cstdlib>`, etc.
3. **C++ standard library headers** — `<vector>`, `<memory>`, `<string>`, etc.
4. **Third-party / external library headers** — e.g. `<boost/...>`, `<fmt/...>`.
5. **Project headers** — other headers from this codebase, using quoted includes (`"..."`).

```cpp
// NetworkManager.cpp
#include "NetworkManager.hpp"   // 1. corresponding header

#include <cstdint>               // 2. C system headers

#include <memory>                // 3. C++ standard library
#include <string>
#include <vector>

#include <fmt/format.h>          // 4. third-party libraries

#include "Logger.hpp"            // 5. project headers
#include "SocketWrapper.hpp"
```

- Use angle brackets `<...>` for system, standard library, and third-party headers.
- Use quotes `"..."` for project-local headers.
- This ordering guarantees each header can be compiled standalone (no hidden dependency on something included before it) and makes missing includes fail fast in CI rather than silently compiling due to include order in some other file.

---

## 8. Line Length

- Prefer a maximum of **100 characters** per line.
- Break long function signatures with one parameter per line, indented once, if needed.

---

## 9. Comments

- Use `//` for single-line comments.
- Use `/** ... */` (Doxygen-style) for documenting public API functions and classes.

```cpp
/**
 * Establishes a connection to the configured server.
 * @return true if the connection succeeded.
 */
bool connectToServer();
```

---

## 10. Language Feature Do's and Don'ts

### Memory and Ownership

| Do | Don't |
|----|-------|
| Use `std::unique_ptr` / `std::shared_ptr` for owning dynamic objects | Use raw `new` / `delete` for ownership |
| Use raw pointers or references for **non-owning** access | Use a raw pointer to express or transfer ownership |
| Use `std::make_unique` / `std::make_shared` to construct smart pointers | Construct a smart pointer with `new` directly (`std::unique_ptr<T>(new T())`) |
| Follow RAII for all resources (files, locks, sockets, etc.) | Manually pair acquire/release calls |

### Error Handling

| Do | Don't |
|----|-------|
| Throw exceptions for **true exceptional situations** (unrecoverable errors, invariant violations, constructor failures) | Use exceptions for expected/recoverable control flow (e.g. "value not found") |
| Use return values (`bool`, `std::optional`, error codes, `std::expected` where available) for expected failure cases | Use exceptions as a substitute for normal branching logic |
| Catch exceptions by `const&` | Catch exceptions by value (causes slicing) |
| Keep destructors `noexcept` (the default) | Let exceptions escape a destructor |

### Modern C++ Features

| Do | Don't |
|----|-------|
| Use `auto` when it improves readability (iterators, lambdas, long template types) | Use `auto` when it hides an important or non-obvious type |
| Use range-based `for` loops over index-based loops when the index isn't needed | Write manual index loops just to iterate a container |
| Use lambdas for short, local callbacks and algorithm predicates | Write a named functor class for a one-off, trivial operation |
| Use structured bindings for multi-value returns (`auto [key, value] = ...`) | Unpack pairs/tuples manually with `.first` / `.second` when structured bindings are clearer |
| Use `constexpr` / `consteval` for compile-time constants and computations | Use `#define` macros for constants |
| Use `enum class` over plain `enum` | Use unscoped `enum` (pollutes namespace, weak typing) |
| Use `override` and `final` explicitly on overriding virtual functions | Omit `override`, relying on the compiler to catch signature mismatches |
| Use `nullptr` | Use `NULL` or `0` for pointers |
| Prefer `std::array` / `std::vector` over C-style arrays | Use raw C-style arrays for anything beyond fixed, tiny buffers |
| Pass small, cheap-to-copy types by value; pass large objects by `const&` | Pass large objects by value out of habit |
| Use `std::string_view` for read-only, non-owning string parameters | Take `const std::string&` when the caller may pass a string literal or substring |

---

## 11. Library Linking and Runtime Loading

How a component is built and linked shapes how strictly some of the above rules should be enforced.

### Static Linking

- Libraries are compiled directly into the final binary at build time.
- Header/implementation split still matters for compile-time isolation, but ABI stability across the boundary is **not** a concern — the whole binary is rebuilt together.
- Inline-heavy, template-heavy, and header-only code (common with `auto`, templates, `constexpr`) is safe here since everything resolves at compile time in one binary.

### Dynamic Linking (Shared Libraries)

- Applies to `.so` (Linux), `.dll` (Windows), and `.dylib` (macOS) targets.
- The public headers exposed across a shared-library boundary form an **ABI contract** — style choices here have real consequences:
  - Avoid exposing `inline` functions, templates, or `constexpr` values in the public API surface if the library and its consumers may be compiled by different compilers/versions — inlined code bakes assumptions into the caller.
  - Avoid changing the layout of any `class`/`struct` exposed across the boundary (adding/reordering member variables) without a version bump — it breaks binary compatibility for already-compiled consumers.
  - Prefer exposing a stable, minimal interface at the boundary (see [Part II: C API Style](#part-ii-c-api-style)), and keep the `PascalCase` classes with full modern C++ usage as **internal implementation detail** behind it.
  - Mark exported symbols explicitly (e.g. `__declspec(dllexport)` / `__attribute__((visibility("default")))`), and keep everything else hidden by default, to keep the ABI surface intentional and small.

### Runtime Loading (dlopen and LoadLibrary)

- Any function loaded dynamically at runtime **must** be declared with C linkage (`extern "C"`) to avoid name-mangling issues — this is exactly the case [Part II: C API Style](#part-ii-c-api-style) exists for, even in an otherwise all-`.hpp` C++ codebase.
- Keep the loaded interface as a plain function-pointer table or `extern "C"` API — no classes, no exceptions crossing the boundary (an exception thrown from a plugin loaded via `dlopen` and caught in the host app is undefined behavior across incompatible runtimes/compilers).
- Document plugin/module boundaries clearly with `.hpp` interface headers describing the contract, even though the actual crossing point is a plain C-style vtable or function table.

### Summary

| Context | Modern C++ features across the boundary | ABI stability required |
|---|---|---|
| Static link | Freely (templates, `auto`, inline, `constexpr`) | No — whole program rebuilt together |
| Dynamic link (`.so`/`.dll`) | Restrict at the public boundary; free internally | Yes — layout & symbol changes are breaking |
| Runtime load (`dlopen`) | `extern "C"` only at the boundary ([Part II](#part-ii-c-api-style) style) | Yes — plus no exceptions/classes across it |

> **See also:** [Boundary Rule of Thumb](#17-boundary-rule-of-thumb) — decides, file by file, whether a piece of code follows this section or Part II · [Part II: C API Style](#part-ii-c-api-style) — the actual convention referenced throughout this section.

---

## 12. Miscellaneous

- Mark single-argument constructors `explicit` unless implicit conversion is intended.
- Prefer `const` correctness everywhere it applies.

---

# Part II: C API Style

This section governs any code written as **plain C**, or any `extern "C"` surface exposed *from* a C++ library for consumption by C callers, dynamic loaders (`dlopen`), or other languages via FFI. Nothing in this section applies to internal C++ implementation — only to the boundary itself and to files that are genuinely compiled as C.

## 13. Naming Conventions (C API)

| Symbol Type            | Convention           | Example                              |
|-------------------------|----------------------|----------------------------------------|
| Public API functions     | `snake_case` with library prefix | `net_manager_connect(NetManager* mgr);` |
| Types (`struct`/`enum`/typedef) | `PascalCase` with library prefix, `_t` suffix optional | `NetManagerT` or `NetManager` |
| Struct fields             | `snake_case`          | `int connection_timeout;`             |
| Function parameters        | `snake_case`           | `int retry_count`                      |
| Constants / `#define`        | `ALL_CAPS_SNAKE_CASE` with library prefix | `#define NET_MAX_RETRIES 5`         |
| Enum values                  | `ALL_CAPS_SNAKE_CASE` with library + enum prefix | `NET_STATE_CONNECTED`             |
| Internal global variables (translation-unit-visible, mutable) | `g_snake_case` | `int g_active_connection_count;` |
| Static variables (function-local or file-scope `static`) | `s_snake_case` | `static int s_retry_attempts;` |
| File names                    | `snake_case`            | `net_manager.h` / `net_manager.c`      |

- Every public symbol (function, type, macro, enum value) is **prefixed with the library/module name** to avoid collisions in the global C symbol namespace, since C has no namespaces.
- `snake_case` is used throughout the C API — this is a deliberate departure from the `camelCase`/`PascalCase` used in Part I, and is one of the clearest visual signals that a symbol belongs to the C boundary rather than internal C++.
- The `g_`/`s_` prefixes are for **internal, non-exported** state — the kind of file-scope `static` variable that never appears in a public header. A genuinely public global exported from the API surface is unusual enough that it should get the full library prefix instead (`net_manager_default_timeout`, not `g_net_manager_default_timeout`) — don't stack both prefixes on the same symbol.

---

## 14. File Extensions and Structure

- **`.h`** for all public C API headers — must be valid to include from both C and C++ translation units.
- **`.c`** for files actually compiled as C. If the "C API" is really a C++ implementation exposing an `extern "C"` shim, the shim itself lives in a `.cpp` file but declares its exported functions in a `.h` header guarded for C++ (see below).
- One `.h` per logical module/library surface (e.g. `net_manager.h`), mirroring the one-header-per-component rule from Part I, but scoped to the *public* API rather than one per internal class.

```c
/* net_manager.h */
#ifndef NET_MANAGER_H
#define NET_MANAGER_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct NetManager NetManager;

NetManager* net_manager_create(void);
void net_manager_destroy(NetManager* mgr);
int net_manager_connect(NetManager* mgr, const char* host, int port);

#ifdef __cplusplus
}
#endif

#endif /* NET_MANAGER_H */
```

- Use traditional `#ifndef`/`#define`/`#endif` header guards in `.h` files rather than `#pragma once` — `.h` files must remain portable to strict C compilers where `#pragma once` support is less universally guaranteed.
- Always wrap declarations in `extern "C" { ... }` guarded by `#ifdef __cplusplus`, so the same header works whether included from a C or a C++ translation unit.

---

## 15. Indentation and Braces

Match Part I for consistency across the codebase: **tabs** for indentation, **Allman** brace placement.

```c
int net_manager_connect(NetManager* mgr, const char* host, int port)
{
	if (mgr == NULL)
	{
		return NET_ERROR_INVALID_ARGUMENT;
	}

	return connect_impl(mgr, host, port);
}
```

---

## 16. API Design Do's and Don'ts

| Do | Don't |
|----|-------|
| Return an integer error code / status enum from every fallible function | Throw or let a C++ exception cross into `extern "C"` code (undefined behavior for C callers) |
| Use opaque pointers (`typedef struct NetManager NetManager;` with the definition hidden in the `.c`/`.cpp`) to hide implementation details and preserve ABI stability | Expose full `struct` layouts in the public header if the layout may change — this breaks binary compatibility |
| Provide explicit `_create` / `_destroy` (or `_init` / `_free`) pairs for any type owning resources | Rely on constructors/destructors (C has none) or leave lifetime management implicit |
| Take a `void* user_data` parameter on any callback-accepting function | Assume the caller can supply capturing closures — plain C function pointers can't capture state |
| Check all pointer parameters for `NULL` before dereferencing at public entry points | Assume public API callers always pass valid pointers |
| Keep the header C89/C99-compatible (no `//` comments, no variable declarations mid-block if targeting strict C89) unless the project's minimum C standard is confirmed higher | Use C++-only syntax anywhere in a `.h` that must remain includable from C |
| Document ownership transfer explicitly in comments (e.g. "caller owns the returned pointer and must call `net_manager_destroy`") | Leave ownership of returned pointers ambiguous |

---

## 17. Boundary Rule of Thumb

- If a file is `.cpp`/`.hpp` and never crosses a dynamic-library, plugin, or FFI boundary: **use [Part I](#part-i-c-style), full stop.**
- If a file declares or implements symbols meant to be called from C, another language, or loaded via `dlopen`/`LoadLibrary`: **use [Part II](#part-ii-c-api-style)** for that surface, even if the implementation behind it is C++.
- Never mix the two conventions within the same file — a `.h` should never contain `camelCase` C++-flavored declarations, and a `.hpp` should never contain `snake_case` C-flavored ones. Where a C++ class needs a C API, write a thin `extern "C"` shim (Part II style) that internally calls into the real C++ implementation (Part I style).

> **See also:** [Library Linking and Runtime Loading](#11-library-linking-and-runtime-loading) — the dynamic-library and `dlopen` boundaries that make this rule necessary in the first place.
