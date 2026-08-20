#pragma once

// The type system. A `Scalar` is a closed variant over every primitive
// type the engine understands. A `Value` wraps either a single Scalar
// (for ordinary parameters/options) or a vector of them (for variadic
// parameters/options). `TypeInfo` bundles a display name with a parser:
// for the built-in scalar types this is a *plain stateless function
// pointer* (no heap allocation, unlike std::function), and only the
// rarer enum/choice-restricted case pays for a one-time, shared, heap
// allocation to hold its name table.

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

namespace cliforge
{

	// ---------------------------------------------------------------------
	// Exceptions
	// ---------------------------------------------------------------------

	// Every distinct reason a run-time parse can fail. Exposed on CliError so
	// callers (and Engine internally, for its own error-selection logic) can
	// branch on *why* something failed, not just read a free-text message.
	enum class ErrorKind
	{
		Generic,
		UnknownOption,	  // a flag/option name isn't recognized for this command
		TooManyArguments, // more positional values than the command declares
		MissingArgument,  // a required positional value (or option value) was never given
		MissingValue,	  // an option/flag was named but its value token is missing
		TypeMismatch,	  // a token couldn't convert to the slot's declared type
	};

	// Base of every error the engine raises while registering commands or
	// parsing user input. Engine::run() catches CliError and prints it
	// nicely instead of letting it escape as an unhandled exception.
	class CliError : public std::runtime_error
	{
	public:
		explicit CliError(const std::string& msg, ErrorKind kind = ErrorKind::Generic);
		ErrorKind kind() const noexcept;	

	private:
		ErrorKind kind_;
	};

	// A token couldn't be converted to the type a slot expects -- always
	// ErrorKind::TypeMismatch.
	class ParseError : public CliError
	{
	public:
		explicit ParseError(const std::string& msg); 
	};

	// Something is wrong with how a command was *built* (mismatched arity
	// between declared slots and the bound function, duplicate names, a
	// choices<T>() call whose T doesn't match the declared type, an option
	// bound to a non-optional type, an action() parameter type that doesn't
	// match what was declared, etc). These fire at program startup (many of
	// them at compile time via static_assert), not at end-user parse time.
	struct RegistrationError : CliError
	{
		explicit RegistrationError(const std::string& msg); 
	};

	// ---------------------------------------------------------------------
	// Enum storage
	// ---------------------------------------------------------------------

	// Custom enums aren't a native Scalar alternative (there are infinitely
	// many enum types), so when a slot is restricted to an enum via
	// choices<T>(), the underlying value is boxed here. Value::as<T>() knows
	// to unbox + static_cast back to the caller's real enum type.
	struct EnumTag
	{
		long long value;
	};

	// ---------------------------------------------------------------------
	// Scalar: the closed set of representable primitive values
	// ---------------------------------------------------------------------

	using Scalar = std::variant<int8_t, uint8_t, int16_t, uint16_t, int32_t, uint32_t, int64_t,
								uint64_t, float, double, std::string, char, bool, EnumTag>;

	enum class Kind
	{
		I8,
		U8,
		I16,
		U16,
		I32,
		U32,
		I64,
		U64,
		F32,
		F64,
		Str,
		Char,
		Bool,
		Enum
	};

	// ---------------------------------------------------------------------
	// TypeInfo: how to parse + describe one type
	// ---------------------------------------------------------------------

	// Holds the name table for an enum/choice-restricted slot. Built once at
	// registration time (a single, amortized allocation via shared_ptr);
	// parsing against it never allocates beyond the error path.
	struct ChoiceTable
	{
		std::vector<std::pair<std::string, Scalar>> entries;

		Scalar parse(std::string_view token, std::string_view label) const;	
	};

	struct TypeInfo
	{
		Kind kind{};
		std::string displayName;
		// Stateless parser for built-in scalar types -- a plain function
		// pointer costs nothing to store or copy, unlike std::function.
		Scalar (*parsePlain)(std::string_view token, std::string_view label) = nullptr;
		// Populated only for choices<T>()-restricted slots.
		std::shared_ptr<const ChoiceTable> choiceTable;

		bool valid() const;
		Scalar parse(std::string_view token, std::string_view label) const;
		
	};

	namespace detail
	{
		template <typename Int>
		Int parseInteger(std::string_view token, std::string_view label, std::string_view typeName)
		{
			Int out{};
			auto [ptr, ec] = std::from_chars(token.data(), token.data() + token.size(), out);
			if (ec != std::errc{} || ptr != token.data() + token.size())
			{
				throw ParseError("type mismatch for '" + std::string(label) + "': got '" +
								 std::string(token) + "', expected " + std::string(typeName));
			}
			return out;
		}

		template <typename Float>
		Float parseFloating(std::string_view token, std::string_view label,
							std::string_view typeName)
		{
			Float out{};
			auto [ptr, ec] = std::from_chars(token.data(), token.data() + token.size(), out);
			if (ec != std::errc{} || ptr != token.data() + token.size())
			{
				throw ParseError("type mismatch for '" + std::string(label) + "': got '" +
								 std::string(token) + "', expected " + std::string(typeName));
			}
			return out;
		}

		std::string toLower(std::string_view s);
	}

	// Maps a C++ type T to its Kind + a ready-to-use TypeInfo. Specialized
	// below for every built-in type; enums go through choices<T>() instead
	// since they need a name<->value table supplied by the caller.
	template <typename T> struct TypeOf; // intentionally undefined for unsupported T

#define CLIFORGE_INT_TYPE(CPP_T, KIND, NAME)                                                       \
	template <> struct TypeOf<CPP_T>                                                               \
	{                                                                                              \
		static TypeInfo get()                                                                      \
		{                                                                                          \
			TypeInfo t;                                                                            \
			t.kind = Kind::KIND;                                                                   \
			t.displayName = NAME;                                                                  \
			t.parsePlain = [](std::string_view tok, std::string_view lbl) -> Scalar                \
			{ return Scalar{detail::parseInteger<CPP_T>(tok, lbl, NAME)}; };                       \
			return t;                                                                              \
		}                                                                                          \
	};

	CLIFORGE_INT_TYPE(int8_t, I8, "int8")
	CLIFORGE_INT_TYPE(uint8_t, U8, "uint8")
	CLIFORGE_INT_TYPE(int16_t, I16, "int16")
	CLIFORGE_INT_TYPE(uint16_t, U16, "uint16")
	CLIFORGE_INT_TYPE(int32_t, I32, "int32")
	CLIFORGE_INT_TYPE(uint32_t, U32, "uint32")
	CLIFORGE_INT_TYPE(int64_t, I64, "int64")
	CLIFORGE_INT_TYPE(uint64_t, U64, "uint64")
#undef CLIFORGE_INT_TYPE

	template <> struct TypeOf<float>
	{
		static TypeInfo get()
		{
			TypeInfo t;
			t.kind = Kind::F32;
			t.displayName = "float32";
			t.parsePlain = [](std::string_view tok, std::string_view lbl) -> Scalar
			{ return Scalar{detail::parseFloating<float>(tok, lbl, "float32")}; };
			return t;
		}
	};

	template <> struct TypeOf<double>
	{
		static TypeInfo get()
		{
			TypeInfo t;
			t.kind = Kind::F64;
			t.displayName = "float64";
			t.parsePlain = [](std::string_view tok, std::string_view lbl) -> Scalar
			{ return Scalar{detail::parseFloating<double>(tok, lbl, "float64")}; };
			return t;
		}
	};

	template <> struct TypeOf<std::string>
	{
		static TypeInfo get()
		{
			TypeInfo t;
			t.kind = Kind::Str;
			t.displayName = "string";
			t.parsePlain = [](std::string_view tok, std::string_view) -> Scalar
			{ return Scalar{std::string(tok)}; };
			return t;
		}
	};

	template <> struct TypeOf<char>
	{
		static TypeInfo get()
		{
			TypeInfo t;
			t.kind = Kind::Char;
			t.displayName = "char";
			t.parsePlain = [](std::string_view tok, std::string_view lbl) -> Scalar
			{
				if (tok.size() != 1)
				{
					throw ParseError("type mismatch for '" + std::string(lbl) + "': got '" +
									 std::string(tok) + "', expected a single char");
				}
				return Scalar{tok[0]};
			};
			return t;
		}
	};

	template <> struct TypeOf<bool>
	{
		static TypeInfo get()
		{
			TypeInfo t;
			t.kind = Kind::Bool;
			t.displayName = "bool";
			t.parsePlain = [](std::string_view tok, std::string_view lbl) -> Scalar
			{
				std::string lower = detail::toLower(tok);
				if (lower == "true" || lower == "1" || lower == "yes" || lower == "on")
				{
					return Scalar{true};
				}
				if (lower == "false" || lower == "0" || lower == "no" || lower == "off")
				{
					return Scalar{false};
				}
				throw ParseError("type mismatch for '" + std::string(lbl) + "': got '" +
								 std::string(tok) +
								 "', expected bool (true/false, 1/0, yes/no, on/off)");
			};
			return t;
		}
	};

	// Builds a TypeInfo for a `choices<T>()` restricted slot: only the
	// listed string keys are accepted, each mapped to a T. Works both for
	// genuine C++ enums (boxed into EnumTag) and for restricting a built-in
	// type (e.g. a string option limited to a fixed set of values). The
	// choice table is converted to type-erased Scalars once, here, so
	// nothing downstream needs to be templated on T again.
	template <typename T>
	TypeInfo makeChoiceTypeInfo(std::string typeName,
								const std::vector<std::pair<std::string, T>>& choices)
	{
		auto table = std::make_shared<ChoiceTable>();
		table->entries.reserve(choices.size());
		for (const auto& [name, val] : choices)
		{
			if constexpr (std::is_enum_v<T>)
			{
				table->entries.emplace_back(name, Scalar{EnumTag{static_cast<long long>(val)}});
			}
			else
			{
				table->entries.emplace_back(name, Scalar{val});
			}
		}
		TypeInfo t;
		t.kind = Kind::Enum;
		t.displayName = std::move(typeName);
		t.choiceTable = std::move(table);
		return t;
	}

	// ---------------------------------------------------------------------
	// Value: what actually flows out of the parser and into bound functions
	// ---------------------------------------------------------------------

	class Value
	{
	public:
		Value();

		static Value ofScalar(Scalar s);
		static Value ofVector(std::vector<Scalar> v);
		bool hasValue() const;
		bool isVector() const;

		template <typename T> T as() const
		{
			const Scalar& s = std::get<Scalar>(data_);
			if constexpr (std::is_enum_v<T>)
			{
				return static_cast<T>(std::get<EnumTag>(s).value);
			}
			else
			{
				return std::get<T>(s);
			}
		}

		template <typename T> std::vector<T> asVector() const
		{
			const auto& vec = std::get<std::vector<Scalar>>(data_);
			std::vector<T> out;
			out.reserve(vec.size());
			for (const Scalar& s : vec)
			{
				if constexpr (std::is_enum_v<T>)
				{
					out.push_back(static_cast<T>(std::get<EnumTag>(s).value));
				}
				else
				{
					out.push_back(std::get<T>(s));
				}
			}
			return out;
		}

	private:
		std::variant<std::monostate, Scalar, std::vector<Scalar>> data_;
	};
}
