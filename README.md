# CLIForge

A header-only C++20 engine for building CLIs whose commands are ordinary
free functions. Parameters and options are explicitly typed where
they're declared; `.action()` validates (never invents) that the bound
function's arguments are consistent with what was declared, and accepts
them by value, by reference, or by const reference.

```cpp
#include <cliforge/cliforge.hpp>
using cliforge::Engine;

void greet(const std::string& name, bool loud) {
    std::cout << (loud ? "HELLO, " : "Hello, ") << name << (loud ? "!!" : ".") << "\n";
}

int main(int argc, char** argv) {
    cliforge::Engine cli("greeter");
    cli.command()
        .keyword("greet")
        .parameter<std::string>("name", "Who to greet")
        .flag("loud", 'l', "SHOUT the greeting")
        .describe("Greets someone by name")
        .action(&greet);
    return cli.run(argc, argv);
}
```

```
$ greeter greet Ada --loud
HELLO, Ada!!
```

## Try it

```sh
g++ -std=c++20 -Iinclude -O2 -o packctl examples/main.cpp
./packctl                                        # global help
./packctl project myapp create --template cpp --git
./packctl build 42 2 1 -a arm64 x64 -e vim       # variadic params + variadic option
./packctl build 7 -ndi -e nano                   # combined short flags
./packctl deploy web1 --env prod web2 -n web3    # loose parameters, order-agnostic
./packctl project new -h                         # ambiguous prefix -> shows all 3 matches
./packctl projct myapp delete                    # typo -> "did you mean"
```

Or with CMake: `cmake -B build && cmake --build build`, which produces
`packctl` (the demo) plus `type_test` / `registration_errors_test`.

## The model

A command has exactly two parts, matching the spec:

```
packctl deploy web1 --env prod web2 -n web3
        \____/ \___________ unstructured ___________/
      structured    flags + options + loose parameters, any order
```

* **Structured part** -- an ordered, *fixed-position* sequence of literal
  **keywords** (`project`, `delete`) and typed positional **parameters**
  (`<name>`). The user's tokens must line up with this sequence exactly,
  in order. Only the *last* parameter here may be variadic. Keywords
  carry no description of their own -- `.describe()` on the command is
  what shows up in help.
* **Unstructured part** -- **flags** (boolean switches), **options**
  (named, typed values), and additional **loose parameters**, which the
  user may give in any order relative to each other:
  * Flags/options are found by name (`--foo`/`-f`) wherever they appear.
    Short flags combine (`-ndi` == `-n -d -i`); options can't be combined
    since they need a value.
  * Whatever bare (non-dash) tokens are left over are the loose
    parameters, matched *positionally among themselves, in declaration
    order* -- regardless of where they fall relative to the flags/
    options. `deploy web1 --env prod web2 -n web3` and
    `deploy -n web1 web2 --env prod web3` both give `hosts = [web1, web2,
    web3]`.

A command must declare at least one keyword. `.keyword()` can only be
called before any `.flag()`/`.option()`. `.parameter<T>()` can be called
on *either side* of that line: before the first `.flag()`/`.option()`
it's a structured, fixed-position parameter; after, it's a loose one.
Either way, only the last parameter declared in its half may be variadic.

**Parameters are always required** (unless they're the trailing variadic
one, which accepts zero or more). **Options are always optional** --
`.option<T>()` must be bound to `std::optional<T>` or (if variadic)
`std::vector<T>` in `action()`; binding it to a bare, required `T` is a
`RegistrationError`. This mirrors ordinary CLI convention: positional
arguments are the required ones, named flags/options never are.

## Explicit types, validated (not invented) at action()

Every `.parameter<T>()` / `.option<T>()` call requires `T` up front --
types are never inferred from the bound function. `.action(&fn)` deduces
`fn`'s argument types via `function_traits` and *checks* each one against
what was already declared, matched in order:

```
[ parameters, in the order you called .parameter<T>() before any flag/option ]
++
[ flags/options/loose-parameters, in the order you called them after ]
```

| Declared as | `action()`'s argument must be |
|---|---|
| `flag(...)` | `bool` |
| `parameter<T>(...)` | `T` -- by value, `const T&`, or `T&` |
| `parameter<T>(..., variadic=true)` | `std::vector<T>` |
| `option<T>(...)` | `std::optional<T>` (never a bare `T` -- options are always optional) |
| `option<T>(..., variadic=true)` | `std::vector<T>` |
| an `enum class` (either kind) | needs `.choices<T>({{"name", T::Value}, ...})` called immediately after; `T` is deduced from the initializer list |

A mismatch anywhere in this table (wrong arity, a flag bound to
non-`bool`, an option bound to a bare/required type, a declared type
that doesn't match the corresponding argument type, a variadic slot
bound to a non-vector, ...) is a `RegistrationError` at startup -- see
`tests/registration_errors_test.cpp`. An unsupported scalar type (say,
`std::map<int,int>`) fails immediately at the `parameter<T>()`/
`option<T>()` call site via `static_assert`, before `action()` is even
reached.

Arguments can be taken **by value, by reference, or by const
reference** -- whichever avoids an unnecessary copy for the type in
question (`const std::string&`, `const std::vector<T>&`, etc. all work).

## Types

Fixed-width signed/unsigned integers (`int8_t` ... `uint64_t`), `float`,
`double`, `std::string`, `char` (exactly one byte), `bool`
(`true/false/1/0/yes/no/on/off`, case-insensitive), and custom
`enum class` types via `choices<T>()`. Parsing uses `std::from_chars`
for numbers, so `"5abc"` or `"3.14"` handed to an `int32_t` slot is
rejected rather than silently truncated.

## Errors are categorized, not just "something went wrong"

Every runtime parse failure is a `CliError` carrying both a specific,
actionable message and a programmatic `ErrorKind` (`.kind()`):

| Situation | `ErrorKind` | Example message |
|---|---|---|
| A `--flag`/`-x` isn't recognized | `UnknownOption` | `unknown flag/option '--arch' for this command -- did you mean '--architectures'?` |
| More positional values than declared slots can absorb | `TooManyArguments` | `too many arguments: unexpected 'extra' (and 2 more)` |
| A required parameter (or loose parameter) was never given | `MissingArgument` | `missing required argument '<name>': expected a value of type string` |
| An option was named but its value token is missing | `MissingValue` | `option '--editor' requires a value` |
| A token doesn't fit its declared type | `TypeMismatch` | `type mismatch for 'threshold': got 'abc', expected int32` |

## `--help` resolves ambiguity instead of hiding it

`--help`/`-h` (anywhere in the command line) and the `help <words...>`
top-level command both search for *every* command consistent with what
was typed so far -- including when a positional value has already been
filled in. `packctl project new -h` matches `project <name> create`,
`project <name> delete`, and `project <name>` (info) all at once, since
`new` is a perfectly plausible `<name>` for any of them; it prints all
three, clearly separated, rather than silently picking one. A
fully-specified prefix (`project myapp delete --help`) narrows to the
one actual match, exactly as before.

The same treatment applies to an *incomplete* command that wasn't
explicitly asking for help: `packctl project` alone is missing
`<name>`, and that's equally true of all three `project` commands (they
report the identical error). Rather than arbitrarily surfacing whichever
one happened to be registered first, every command tied for "most
specific incomplete match" is listed:

```
$ packctl project
Error: missing required argument '<name>': expected a value of type string
'project' matches 3 commands:
  packctl project <name> create [OPTIONS]       Scaffold a new project from a template
  packctl project <name> delete [OPTIONS]       Permanently delete a project by name
  packctl project <name> [OPTIONS]              Show information about a project
```

A single most-specific match (e.g. `packctl project x create --template`,
missing only `--template`'s value) still reports just the one relevant
`Usage:` line, unchanged.

## Keeping allocations down

* `TypeInfo`'s parser is a **plain stateless function pointer**, not
  `std::function` -- built-in scalar types (the overwhelming majority of
  slots) cost nothing to store or copy. Only `choices<T>()`-restricted
  slots pay for an allocation, once, at registration (a `shared_ptr` to
  a small name table), not per parse.
* **Short flag/option names are `char`**, not `std::string` -- no
  allocation to declare one, and combined short flags (`-ndi`) are
  matched by direct `char` comparison instead of constructing a
  temporary single-character string per letter.
* The unstructured parser (flags/options/loose-parameters) works in
  terms of `std::string_view` internally -- splitting `--name=value` or
  stripping the leading dashes off a token no longer copies; the token's
  bytes are only ever copied once, into the final `Scalar`, when a value
  actually needs to be owned.
* `choices<T>()` no longer goes through `std::any` -- the declared C++
  type is tracked with a `std::type_index` (a couple of pointer-sized
  comparisons, no allocation) and validated directly against `action()`'s
  argument types.

Parsing a single command line still does a handful of small, one-shot
`std::vector` allocations (there's no way around owning the parsed
values), so if you're embedding cliforge in a long-running REPL parsing
many commands per process, reusing buffers across calls would be the
next lever -- not implemented here, since it's not needed for the
"parse argv once and dispatch" shape most CLIs have.

## Design decisions worth knowing about

These are the places where the spec was open to interpretation; here's
what this implementation does and why.

* **Keywords never start with `-`.** The spec allows keywords to start
  with digits, but a leading dash would be indistinguishable from a
  flag. Internal dashes/dots/underscores are fine (`self-update`,
  `2fa`, `v2.load` are all valid keywords).
* **Negative numbers vs. flags.** A token is only treated as "flag/
  option syntax" if it starts with `-` and the next character *isn't a
  digit* -- so `-5`/`-3` parse as negative numeric values (structured
  variadic parameters *or* loose parameters), while `-n` still means the
  flag `n`. Applied consistently in both the structured matcher and the
  unstructured parser.
* **Disambiguating overlapping commands.** `project <name>` and
  `project <name> delete` can both structurally match
  `project myapp delete`. Candidates are ranked by how many *literal*
  keywords matched (more specific wins), with total structured-slot
  count as a tiebreaker, and are tried in that order -- so `delete`
  correctly wins over swallowing `"delete"` into `<name>`.
* **Error attribution when several candidates fail.** If the
  best-ranked candidate's *value* fails to parse or is missing, that's
  almost certainly the user's intended command, so its error is what
  gets reported -- not a generic "no command matches", and not a
  less-relevant error from a lower-ranked candidate that also happened
  to fail.
* **"Did you mean?" is structural, not string-blob.** Comparing the raw
  input against a usage string like `project <name> delete` would
  score badly once you substitute a real value for `<name>`. Instead,
  suggestions walk the same keyword/parameter structure as real
  matching, treating parameter slots as wildcards and only scoring
  edit-distance on the literal keyword text -- normalized to a ratio so
  a variadic parameter silently absorbing unrelated tokens can't make
  garbage input look like a close match. Flag/option suggestions
  additionally treat a prefix relationship (`--arch` for
  `--architectures`) as a strong match even though its raw edit
  distance is large.
* **`--name=value` is supported** alongside the spec's space-separated
  form, as a low-cost, expected CLI convention.
* **Repeated non-variadic options: last write wins** (`--reason a
  --reason b` yields `"b"`). Variadic options/loose-parameters
  accumulate instead.
* **Not implemented:** a `--` "end of options" separator. Given the
  negative-number heuristic already resolves the main practical
  ambiguity, this was left as a documented extension point rather than
  added speculatively.

## Layout

```
include/cliforge/
  value.hpp            Scalar/Value/TypeInfo/ErrorKind -- the type & error system
  slot.hpp              Keyword/Parameter/Flag/Option descriptor
  function_traits.hpp   Return/argument-type deduction for action()
  levenshtein.hpp        Edit distance + prefix-aware closest-match helper
  command.hpp            Builder, matcher, unstructured parser, help text
  engine.hpp              Registration, ranking/dispatch, help resolution
  cliforge.hpp            Aggregator -- the only header you include
examples/main.cpp         "packctl", a demo touching every feature
tests/
  type_test.cpp                    every scalar type, valid + invalid input
  registration_errors_test.cpp     every builder-misuse guard (+ positive cases)
```
