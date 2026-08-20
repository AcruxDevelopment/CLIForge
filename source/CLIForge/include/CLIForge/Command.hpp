#pragma once

// Command is the fluent builder AND the parser for one CLI command. A
// command is built like:
//
//   engine.command()
//       .keyword("project")
//       .parameter<std::string>("name", "Project name")
//       .keyword("delete")
//       .flag("force", 'f', "Skip the confirmation prompt")
//       .option<std::string>("reason", 'r', "Why it's being deleted")
//       .describe("Delete a project by name")
//       .action(&deleteProject);
//
// Every parameter/option is explicitly typed where it's declared, so the
// engine never has to invent a type from the bound function -- `.action()`
// instead *validates* that the function's corresponding argument is a
// consistent binding (bool for a flag; T, std::optional<T>, or
// std::vector<T> for a parameter/option; by value, by reference, or by
// const reference all work). Mismatches are RegistrationErrors caught at
// startup, not silent surprises at run time.

#include <cctype>

#include <algorithm>
#include <functional>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <typeindex>
#include <vector>

#include "FunctionTraits.hpp"
#include "Levenshtein.hpp"
#include "Slot.hpp"
#include "Value.hpp"

namespace cliforge
{
	namespace detail
	{
		// --- small type-trait helpers used only for action() binding ----------

		template <typename T> struct IsOptional : std::false_type
		{
		};

		template <typename T> struct IsOptional<std::optional<T>> : std::true_type
		{
			using Inner = T;
		};

		template <typename T> inline constexpr bool IsOptionalV = IsOptional<T>::value;

		template <typename T> struct IsVector : std::false_type
		{
		};

		template <typename T> struct IsVector<std::vector<T>> : std::true_type
		{
			using Inner = T;
		};

		template <typename T> inline constexpr bool IsVectorV = IsVector<T>::value;

		template <typename T>
		inline constexpr bool IsBuiltinScalarV =
			std::is_same_v<T, int8_t> || std::is_same_v<T, uint8_t> || std::is_same_v<T, int16_t> ||
			std::is_same_v<T, uint16_t> || std::is_same_v<T, int32_t> ||
			std::is_same_v<T, uint32_t> || std::is_same_v<T, int64_t> ||
			std::is_same_v<T, uint64_t> || std::is_same_v<T, float> || std::is_same_v<T, double> ||
			std::is_same_v<T, std::string> || std::is_same_v<T, char> || std::is_same_v<T, bool>;

		// A token is where the unstructured (flags/options) part begins if it
		// starts with a dash and isn't shaped like a negative number -- that way
		// `build -5 -3 -1` (variadic int parameters) still works even though the
		// tokens start with '-'.
		bool looksLikeOptionStart(std::string_view tok);

		bool isValidKeyword(std::string_view s);

		// A human-readable name for a bound action() argument type, used in
		// RegistrationError messages. Only built-in scalars and enums are ever
		// legal here (anything else is already rejected by parameter<T>()'s own
		// static_assert), so this never needs to handle the general case.
		template <typename T> std::string friendlyTypeName()
		{
			if constexpr (IsBuiltinScalarV<T>)
			{
				return TypeOf<T>::get().displayName;
			}
			else if constexpr (std::is_enum_v<T>)
			{
				return "a different enum type";
			}
			else
			{
				return "an unsupported type";
			}
		}
	}

	// Result of trying to line a command's structured (keyword+parameter)
	// signature up against the front of the user's token list.
	struct StructuredMatch
	{
		bool success = false;
		std::size_t consumed = 0; // tokens belonging to the structured part
		int keywordScore = 0;	  // number of literal keywords matched
		std::vector<Value> paramValues;
		// Set when every literal keyword matched but a parameter's value
		// either failed to parse or was never given -- lets the Engine
		// surface this specific, actionable error instead of a generic
		// "no match" + fuzzy suggestions.
		std::string error;
	};

	class Command
	{
	public:
		// Keywords carry no description of their own -- .describe() on the
		// command is what shows up in help; a keyword is purely a literal
		// token to match.
		Command& keyword(const std::string& literal);

		// T is explicit and required: every parameter is typed at the point
		// it's declared, not inferred later from action(). Called before any
		// flag()/option(), this is a fixed-position structured parameter;
		// called after, it's a "loose" parameter matched positionally among
		// other loose parameters regardless of where flags/options fall
		// around it.
		template <typename T>
		Command& parameter(std::string name, std::string description, bool variadic = false)
		{
			if (name.empty())
			{
				throw RegistrationError("parameter name may not be empty");
			}

			Slot s;
			s.kind = SlotKind::Parameter;
			s.name = std::move(name);
			s.description = std::move(description);
			s.variadic = variadic;
			s.cppType = std::type_index(typeid(T));
			if constexpr (std::is_enum_v<T>)
			{
				s.awaitingChoices = true; // choices<T>(...) must follow immediately
			}
			else
			{
				static_assert(detail::IsBuiltinScalarV<T>,
							  "cliforge: unsupported parameter type -- use a fixed-width int, "
							  "float/double, std::string, char, bool, or an enum (with "
							  "choices<T>() called immediately after)");
				s.type = TypeOf<T>::get();
			}
			if (!m_unstructuredStarted)
			{
				if (m_structuredSealed)
				{
					throw RegistrationError("can't declare parameter('" + s.name +
											"') after a variadic parameter -- a variadic parameter "
											"must be the last thing in the structured part");
				}
				m_structured.push_back(std::move(s));
				m_lastSlot = &m_structured.back();
				if (variadic)
					m_structuredSealed = true;
			}
			else
			{
				if (m_looseSealed)
				{
					throw RegistrationError("can't declare parameter('" + s.name +
											"') after a variadic parameter -- a variadic parameter "
											"must be the last parameter declared");
				}

				m_unstructured.push_back(std::move(s));
				m_lastSlot = &m_unstructured.back();
				if (variadic)
					m_looseSealed = true;
			}
			return *this;
		}

		// shortName is 0 (the default) for "no short form".
		Command& flag(std::string longName, char shortName, const std::string& description);

		// T is explicit and required, same as parameter<T>(). Options are
		// always optional in the calling convention (see action()) -- there
		// is no way to declare a "required" option, by design.
		template <typename T>
		Command& option(std::string longName, char shortName, std::string description,
						bool variadic = false)
		{
			longName = stripDashes(std::move(longName));
			validateLongName(longName);
			validateShortName(shortName);
			checkNameFree(longName, shortName);
			Slot s;
			s.kind = SlotKind::Option;
			s.name = std::move(longName);
			s.shortName = shortName;
			s.description = std::move(description);
			s.variadic = variadic;
			s.cppType = std::type_index(typeid(T));
			if constexpr (std::is_enum_v<T>)
			{
				s.awaitingChoices = true;
			}
			else
			{
				static_assert(detail::IsBuiltinScalarV<T>,
							  "cliforge: unsupported option type -- use a fixed-width int, "
							  "float/double, std::string, char, bool, or an enum (with "
							  "choices<T>() called immediately after)");
				s.type = TypeOf<T>::get();
			}
			m_unstructured.push_back(std::move(s));
			m_lastSlot = &m_unstructured.back();
			m_unstructuredStarted = true;
			return *this;
		}

		Command& describe(const std::string& description);

		// Restricts the most-recently-declared parameter/option to a fixed
		// set of named values -- this is how custom enums plug in (T is
		// deduced from the initializer list, no need to spell it out again),
		// and it doubles as a generic "choice" restriction for any type. T
		// must match the type declared at parameter<T>()/option<T>().
		template <typename T>
		Command& choices(std::initializer_list<std::pair<std::string, T>> values,
						 std::string typeName = "")
		{
			if (m_lastSlot == nullptr ||
				(m_lastSlot->kind != SlotKind::Parameter && m_lastSlot->kind != SlotKind::Option))
			{
				throw RegistrationError("choices<>() must directly follow the parameter<T>() or "
										"option<T>() it restricts");
			}

			if (!m_lastSlot->cppType || *m_lastSlot->cppType != std::type_index(typeid(T)))
			{
				throw RegistrationError("choices<>() type doesn't match the type declared for '" +
										m_lastSlot->name + "'");
			}

			std::vector<std::pair<std::string, T>> table(values);
			m_lastSlot->type = makeChoiceTypeInfo<T>(
				typeName.empty() ? std::string("enum") : std::move(typeName), table);
			m_lastSlot->awaitingChoices = false;
			return *this;
		}

		// Binds this command to a free function (or captureless/capturing
		// lambda, or std::function). Argument types are validated against
		// what was already declared via parameter<T>()/option<T>()/flag() --
		// action() never invents a type, it only checks consistency -- and
		// matched, in order, to [parameters in declaration order] then
		// [flags/options/loose-parameters in declaration order]. Arguments
		// may be taken by value, by reference, or by const reference.
		template <typename Func> Command& action(Func f)
		{
			using Traits = detail::FunctionTraits<std::decay_t<Func>>;
			if (m_structured.empty() ||
				std::none_of(m_structured.begin(), m_structured.end(),
							 [](const Slot& s) { return s.kind == SlotKind::Keyword; }))
			{
				throw RegistrationError("a command must declare at least one keyword() so it can "
										"be identified on the command line");
			}

			for (const Slot& s : m_structured)
				checkNotAwaitingChoices(s);
			for (const Slot& s : m_unstructured)
				checkNotAwaitingChoices(s);

			std::vector<Slot*> ordered = orderedBindableSlots();
			if (ordered.size() != Traits::Arity)
			{
				throw RegistrationError(
					"action() function takes " + std::to_string(Traits::Arity) +
					" argument(s) but this command declared " + std::to_string(ordered.size()) +
					" parameter/flag/option slot(s) -- these must match 1:1, "
					"in declaration order (parameters first, then flags/options)");
			}

			bindArgs(std::function{f}, ordered, std::make_index_sequence<Traits::Arity>{});
			m_sealed = true;
			return *this;
		}

		[[nodiscard]] bool sealed() const;
		[[nodiscard]] const std::string& description() const;

		// --- matching / parsing entry points, used by Engine -------------

		[[nodiscard]] StructuredMatch tryMatchStructured(
			const std::vector<std::string>& tokens) const;

		// Parses the (order-agnostic) flags/options/loose-parameters tail.
		// Throws CliError (with a specific ErrorKind) describing exactly what
		// went wrong: an unrecognized flag/option, a missing option value, a
		// missing/extra positional argument, or a type mismatch.
		[[nodiscard]] std::vector<Value> parseUnstructured(
			const std::vector<std::string>& tokens) const;

		void invoke(std::vector<Value> allValues) const;

		[[nodiscard]] std::size_t structuredSlotCount() const;

		// Lenient prefix check used only for --help / `help <words>` : does
		// `tokens` look like the start of (or a complete instance of) this
		// command, without requiring every required argument to actually be
		// present? Keywords still must match exactly; parameter slots accept
		// any token; running out of input mid-command is fine (that's exactly
		// what makes it a "prefix"). Used to find *every* command consistent
		// with what's been typed so far, so an ambiguous prefix can show help
		// for all of them instead of guessing one.
		[[nodiscard]] bool couldMatchPrefix(const std::vector<std::string>& tokens) const;

		// --- help / usage --------------------------------------------------

		[[nodiscard]] std::string usageLine() const;

		[[nodiscard]] std::string helpText(const std::string& progName) const;

		[[nodiscard]] std::vector<std::string> flagOptionNames() const;

		// Fuzzy structural distance used for "did you mean...?" suggestions.
		struct FuzzyScore
		{
			std::size_t distance = 0;
			std::size_t keywordChars = 0; // denominator for a relative threshold
		};

		[[nodiscard]] FuzzyScore fuzzyStructuralDistance(
			const std::vector<std::string>& tokens) const;

	private:
		std::vector<Slot> m_structured;
		std::vector<Slot> m_unstructured;
		std::string m_description;
		std::function<void(std::vector<Value>&)> m_invoke;
		Slot* m_lastSlot = nullptr;
		bool m_unstructuredStarted = false;
		bool m_structuredSealed = false;
		bool m_looseSealed = false;
		bool m_sealed = false;

		static std::string stripDashes(std::string s);

		void validateLongName(const std::string& s) const;

		void validateShortName(char c) const;

		void checkNameFree(const std::string& longName, char shortName) const;

		static void checkNotAwaitingChoices(const Slot& s);

		std::vector<Slot*> orderedBindableSlots();

		// Validates (never invents) that a slot's declared type matches T.
		template <typename T> static void checkTypeMatches(const Slot& slot)
		{
			if (!slot.cppType || *slot.cppType != std::type_index(typeid(T)))
			{
				throw RegistrationError("'" + slot.name + "' was declared as " +
										slot.type.displayName + " but action() expects " +
										detail::friendlyTypeName<T>());
			}
		}

		// ArgT is the *raw* action() parameter type, e.g. `const std::string&`
		// or `std::vector<int32_t>` -- validated after stripping cv/ref.
		template <typename ArgT> static void configureSlot(Slot& slot)
		{
			using Stripped = std::remove_cvref_t<ArgT>;
			if (slot.kind == SlotKind::Flag)
			{
				if constexpr (!std::is_same_v<Stripped, bool>)
				{
					throw RegistrationError("flag '--" + slot.name +
											"' must bind to bool in action()");
				}
				return;
			}
			if constexpr (detail::IsOptionalV<Stripped>)
			{
				using Inner = typename detail::IsOptional<Stripped>::Inner;
				if (slot.kind == SlotKind::Parameter)
				{
					throw RegistrationError("parameter '<" + slot.name +
											">' can't bind to std::optional<> -- positional "
											"parameters are always required");
				}
				if (slot.variadic)
				{
					throw RegistrationError(
						"option '--" + slot.name +
						"' is variadic; bind it to std::vector<>, not std::optional<>");
				}
				checkTypeMatches<Inner>(slot);
			}
			else if constexpr (detail::IsVectorV<Stripped>)
			{
				using Inner = typename detail::IsVector<Stripped>::Inner;
				if (!slot.variadic)
				{
					throw RegistrationError(
						"'" + slot.name +
						"' binds to std::vector<> but wasn't declared variadic=true");
				}
				checkTypeMatches<Inner>(slot);
			}
			else
			{
				if (slot.variadic)
				{
					throw RegistrationError(
						"'" + slot.name + "' was declared variadic=true; bind it to std::vector<>");
				}
				if (slot.kind == SlotKind::Option)
				{
					throw RegistrationError(
						"option '--" + slot.name +
						"' must bind to std::optional<T> (or std::vector<T> if "
						"variadic) -- options are always optional, never required");
				}
				checkTypeMatches<Stripped>(slot);
			}
		}

		template <typename ArgT> static ArgT extractArg(const Value& v)
		{
			if constexpr (std::is_same_v<ArgT, bool>)
			{
				return v.as<bool>();
			}
			else if constexpr (detail::IsOptionalV<ArgT>)
			{
				using Inner = typename detail::IsOptional<ArgT>::Inner;
				return v.hasValue() ? std::optional<Inner>(v.as<Inner>()) : std::nullopt;
			}
			else if constexpr (detail::IsVectorV<ArgT>)
			{
				using Inner = typename detail::IsVector<ArgT>::Inner;
				return v.asVector<Inner>();
			}
			else
			{
				return v.as<ArgT>();
			}
		}

		// Extracted values are materialized into a tuple of *value* types
		// first, then handed to f via std::apply -- std::get on that (named,
		// lvalue) tuple yields lvalues, which is what lets f's parameters be
		// declared by value, by reference, or by const reference and all
		// still bind correctly.
		template <typename Ret, typename... Args, std::size_t... I>
		void bindArgs(std::function<Ret(Args...)> f, std::vector<Slot*>& ordered,
					  std::index_sequence<I...>)
		{
			(configureSlot<Args>(*ordered[I]), ...);
			m_invoke = [f = std::move(f)](std::vector<Value>& values)
			{
				std::tuple<std::remove_cvref_t<Args>...> extracted{
					extractArg<std::remove_cvref_t<Args>>(values[I])...};
				std::apply(f, extracted);
			};
		}

		[[nodiscard]] int findByLong(std::string_view name) const;

		[[nodiscard]] int findByShort(char c) const;

		[[nodiscard]] std::string suggestFlagOption(const std::string& given) const;

		[[noreturn]] void throwUnknown(std::string_view tok) const;

		static std::string describeType(const Slot& slot);

		static std::string padRight(const std::string& s, std::size_t width);

		static void consumeSlot(const Command& self, int idxIn, bool hasInline,
								std::string_view inlineValue,
								const std::vector<std::string>& tokens, std::size_t& i,
								std::vector<bool>& provided,
								std::vector<std::vector<Scalar>>& collected,
								std::string_view tokForError);
	};
}
