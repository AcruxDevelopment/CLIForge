#pragma once

// A `Slot` is one piece of a command's signature: a literal keyword, a
// typed positional parameter, a boolean flag, or a typed option. The
// same struct is reused for all four so Command can keep them in plain
// vectors and the matcher/parser code doesn't need four parallel types.

#include <optional>
#include <string>
#include <typeindex>

#include "Value.hpp"

namespace cliforge
{
	enum class SlotKind
	{
		Keyword,
		Parameter,
		Flag,
		Option
	};

	struct Slot
	{
		SlotKind kind{};

		// Keyword: the literal text the user must type (e.g. "delete").
		// Parameter: the display name (e.g. "name" -> shown as <name>).
		// Flag/Option: the long name WITHOUT the leading "--" (e.g. "force").
		std::string name;

		// Flags/Options only: single-letter short name (e.g. 'f'), or '\0'
		// if no short form was registered. A plain char instead of a string
		// avoids a heap allocation (and the temptation to compare via
		// std::string) for what is always exactly zero or one character.
		char shortName = '\0';

		std::string description;

		// Parameter/Option only: may this slot consume more than one token?
		// Only the *last* structured parameter, or the *last* loose
		// parameter, or any option, may be variadic.
		bool variadic = false;

		// Set immediately by parameter<T>()/option<T>(), or by choices<T>(),
		// never deferred -- so the type is always known and displayable even
		// before action() is called.
		TypeInfo type;

		// The exact C++ type T declared via parameter<T>()/option<T>(), kept
		// so action() can *validate* (not invent) the bound function's
		// corresponding argument type. Unset for Keyword/Flag slots.
		std::optional<std::type_index> cppType;

		// True from parameter<T>()/option<T>() (when T is an enum) until the
		// matching choices<T>() call supplies the name table; action() checks
		// this is false for every slot before binding.
		bool awaitingChoices = false;

		[[nodiscard]] std::string longFlag() const;
		[[nodiscard]] std::string shortFlag() const;
	};
}
