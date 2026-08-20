#include "CLIForge/Command.hpp"

namespace cliforge
{
	namespace detail
	{
		bool looksLikeOptionStart(std::string_view tok)
		{
			if (tok.size() < 2 || tok[0] != '-')
				return false;
			if (tok[1] == '-')
				return true; // "--foo"
			if (std::isdigit(static_cast<unsigned char>(tok[1])))
				return false; // "-5"
			return true;	  // "-n", "-ndi"
		}

		bool isValidKeyword(std::string_view s)
		{
			if (s.empty())
				return false;
			if (s[0] == '-')
				return false; // reserved for flags/options
			for (char c : s)
			{
				if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '_' ||
					  c == '-'))
				{
					return false;
				}
			}
			return true;
		}
	}

	Command& Command::keyword(const std::string& literal)
	{
		if (m_unstructuredStarted)
		{
			throw RegistrationError("can't declare keyword('" + literal +
									"') after a flag()/option() was added -- keywords must "
									"come entirely before the unstructured part");
		}
		if (m_structuredSealed)
		{
			throw RegistrationError("can't declare keyword('" + literal +
									"') after a variadic parameter -- a variadic parameter "
									"must be the last thing in the structured part");
		}
		if (!detail::isValidKeyword(literal))
		{
			throw RegistrationError("invalid keyword '" + literal +
									"': keywords may contain letters, digits, '.', '_' and "
									"'-', and may not start with '-'");
		}
		Slot s;
		s.kind = SlotKind::Keyword;
		s.name = std::move(literal);
		m_structured.push_back(std::move(s));
		m_lastSlot = &m_structured.back();
		return *this;
	}

	// shortName is 0 (the default) for "no short form".
	Command& Command::flag(std::string longName, char shortName, const std::string& description)
	{
		longName = stripDashes(std::move(longName));
		validateLongName(longName);
		validateShortName(shortName);
		checkNameFree(longName, shortName);
		Slot s;
		s.kind = SlotKind::Flag;
		s.name = std::move(longName);
		s.shortName = shortName;
		s.description = std::move(description);
		m_unstructured.push_back(std::move(s));
		m_lastSlot = &m_unstructured.back();
		m_unstructuredStarted = true;
		return *this;
	}

	Command& Command::describe(const std::string& description)
	{
		m_description = std::move(description);
		return *this;
	}

	bool Command::sealed() const
	{
		return m_sealed;
	}

	const std::string& Command::description() const
	{
		return m_description;
	}

	// --- matching / parsing entry points, used by Engine -------------

	StructuredMatch Command::tryMatchStructured(const std::vector<std::string>& tokens) const
	{
		StructuredMatch m;
		std::size_t i = 0;
		try
		{
			for (std::size_t s = 0; s < m_structured.size(); ++s)
			{
				const Slot& slot = m_structured[s];
				bool isLast = (s + 1 == m_structured.size());
				if (slot.kind == SlotKind::Keyword)
				{
					if (i >= tokens.size() || tokens[i] != slot.name)
						return StructuredMatch{};
					++i;
					++m.keywordScore;
				}
				else
				{
					if (slot.variadic && isLast)
					{
						std::vector<Scalar> collected;
						while (i < tokens.size() && !detail::looksLikeOptionStart(tokens[i]))
						{
							collected.push_back(slot.type.parse(tokens[i], slot.name));
							++i;
						}

						m.paramValues.push_back(Value::ofVector(std::move(collected)));
					}
					else
					{
						if (i >= tokens.size() || detail::looksLikeOptionStart(tokens[i]))
						{
							// Every keyword up to here matched literally, so
							// this is almost certainly the intended command
							// -- just missing a required argument.
							throw CliError("missing required argument '<" + slot.name +
											   ">': expected a value of type " +
											   slot.type.displayName,
										   ErrorKind::MissingArgument);
						}

						m.paramValues.push_back(
							Value::ofScalar(slot.type.parse(tokens[i], slot.name)));
						++i;
					}
				}
			}
		}
		catch (const CliError& e)
		{
			// Either a type mismatch (ParseError, a CliError) or the
			// MissingArgument thrown just above -- both mean "this is the
			// command you meant, but something about the value is wrong",
			// so remember it instead of treating it as a plain non-match.
			m.success = false;
			m.error = e.what();
			return m;
		}
		m.success = true;
		m.consumed = i;
		return m;
	}

	// Parses the (order-agnostic) flags/options/loose-parameters tail.
	// Throws CliError (with a specific ErrorKind) describing exactly what
	// went wrong: an unrecognized flag/option, a missing option value, a
	// missing/extra positional argument, or a type mismatch.
	std::vector<Value> Command::parseUnstructured(const std::vector<std::string>& tokens) const
	{
		std::vector<bool> provided(m_unstructured.size(), false);
		std::vector<std::vector<Scalar>> collected(m_unstructured.size());
		std::vector<std::string_view> looseTokens; // views into `tokens`, which outlives this call
		looseTokens.reserve(tokens.size());

		std::size_t i = 0;
		while (i < tokens.size())
		{
			std::string_view tok = tokens[i];
			if (!detail::looksLikeOptionStart(tok))
			{
				// Not flag/option syntax (includes negative numbers like
				// "-5") -- it's a value for the next loose parameter.
				looseTokens.push_back(tok);
				++i;
				continue;
			}
			if (tok[1] == '-')
			{
				std::string_view rest = tok.substr(2);
				auto eq = rest.find('=');
				bool hasInline = (eq != std::string_view::npos);
				std::string_view longName = hasInline ? rest.substr(0, eq) : rest;
				std::string_view inlineValue = hasInline ? rest.substr(eq + 1) : std::string_view{};
				int idx = findByLong(longName);
				if (idx < 0)
					throwUnknown(tok);
				++i;
				consumeSlot(*this, idx, hasInline, inlineValue, tokens, i, provided, collected,
							tok);
			}
			else
			{
				std::string_view rest = tok.substr(1);
				if (rest.size() == 1)
				{
					int idx = findByShort(rest[0]);
					if (idx < 0)
						throwUnknown(tok);
					++i;
					consumeSlot(*this, idx, false, std::string_view{}, tokens, i, provided,
								collected, tok);
				}
				else
				{
					// Combined short flags, e.g. "-ndi" == -n -d -i.
					for (char c : rest)
					{
						int idx = findByShort(c);
						if (idx < 0)
						{
							throw CliError(std::string("unknown flag '-") + c +
											   "' in combined group '" + std::string(tok) + "'" +
											   suggestFlagOption(std::string("-") + c),
										   ErrorKind::UnknownOption);
						}
						if (m_unstructured[static_cast<size_t>(idx)].kind != SlotKind::Flag)
						{
							throw CliError(std::string("'-") + c + "' in combined group '" +
										   std::string(tok) +
										   "' is an option (needs a value), so it can't "
										   "be combined with other short flags");
						}
						provided[static_cast<size_t>(idx)] = true;
					}
					++i;
				}
			}
		}

		// Assign the collected loose tokens to any declared loose
		// Parameter slots, positionally, in declaration order -- this is
		// independent of where those tokens fell relative to flags/options.
		std::vector<std::size_t> looseParamIdx;
		for (std::size_t idx = 0; idx < m_unstructured.size(); ++idx)
		{
			if (m_unstructured[idx].kind == SlotKind::Parameter)
				looseParamIdx.push_back(idx);
		}
		std::vector<Value> looseValues(looseParamIdx.size());
		std::size_t li = 0;
		for (std::size_t k = 0; k < looseParamIdx.size(); ++k)
		{
			const Slot& slot = m_unstructured[looseParamIdx[k]];
			bool isLastLoose = (k + 1 == looseParamIdx.size());
			if (slot.variadic && isLastLoose)
			{
				std::vector<Scalar> vals;
				vals.reserve(looseTokens.size() > li ? looseTokens.size() - li : 0);
				while (li < looseTokens.size())
				{
					vals.push_back(slot.type.parse(looseTokens[li], slot.name));
					++li;
				}
				looseValues[k] = Value::ofVector(std::move(vals));
			}
			else
			{
				if (li >= looseTokens.size())
				{
					throw CliError("missing required argument '<" + slot.name +
									   ">': expected a value of type " + slot.type.displayName,
								   ErrorKind::MissingArgument);
				}
				looseValues[k] = Value::ofScalar(slot.type.parse(looseTokens[li], slot.name));
				++li;
			}
		}
		if (li < looseTokens.size())
		{
			std::size_t extra = looseTokens.size() - li;
			std::string msg =
				"too many arguments: unexpected '" + std::string(looseTokens[li]) + "'";
			if (extra > 1)
				msg += " (and " + std::to_string(extra - 1) + " more)";
			throw CliError(msg, ErrorKind::TooManyArguments);
		}

		// Assemble the final values in m_unstructured's declaration order --
		// this is what gets paired with the bound function's arguments.
		std::vector<Value> out;
		out.reserve(m_unstructured.size());
		std::size_t looseCursor = 0;
		for (std::size_t idx = 0; idx < m_unstructured.size(); ++idx)
		{
			const Slot& slot = m_unstructured[idx];
			if (slot.kind == SlotKind::Parameter)
			{
				out.push_back(std::move(looseValues[looseCursor++]));
			}
			else if (slot.kind == SlotKind::Flag)
			{
				out.push_back(Value::ofScalar(Scalar{provided[idx]}));
			}
			else if (slot.variadic)
			{
				out.push_back(Value::ofVector(collected[idx]));
			}
			else if (provided[idx])
			{
				out.push_back(Value::ofScalar(collected[idx][0]));
			}
			else
			{
				out.push_back(Value{}); // options are always optional -> unset means nullopt
			}
		}
		return out;
	}

	void Command::invoke(std::vector<Value> allValues) const
	{
		m_invoke(allValues);
	}

	std::size_t Command::structuredSlotCount() const
	{
		return m_structured.size();
	}

	bool Command::couldMatchPrefix(const std::vector<std::string>& tokens) const
	{
		std::size_t i = 0;
		for (std::size_t s = 0; s < m_structured.size(); ++s)
		{
			if (i >= tokens.size())
				break; // ran out of input -- a valid, incomplete prefix
			const Slot& slot = m_structured[s];
			bool isLast = (s + 1 == m_structured.size());
			if (slot.kind == SlotKind::Keyword)
			{
				if (tokens[i] != slot.name)
					return false;
				++i;
			}
			else if (slot.variadic && isLast)
			{
				while (i < tokens.size() && !detail::looksLikeOptionStart(tokens[i]))
					++i;
			}
			else
			{
				if (detail::looksLikeOptionStart(tokens[i]))
					break; // structured part left incomplete
				++i;
			}
		}
		// Whatever tokens remain must be plausible flags/options (assumed
		// fine without deep validation) or fit a declared loose parameter.
		std::vector<const Slot*> looseParams;
		for (const Slot& s : m_unstructured)
		{
			if (s.kind == SlotKind::Parameter)
				looseParams.push_back(&s);
		}
		std::size_t looseIdx = 0;
		for (; i < tokens.size(); ++i)
		{
			if (detail::looksLikeOptionStart(tokens[i]))
				continue;
			if (looseIdx >= looseParams.size())
				return false; // nothing left to absorb this token
			if (!looseParams[looseIdx]->variadic)
				++looseIdx;
		}
		return true;
	}

	// --- help / usage --------------------------------------------------

	std::string Command::usageLine() const
	{
		std::ostringstream os;
		for (std::size_t s = 0; s < m_structured.size(); ++s)
		{
			const Slot& slot = m_structured[s];
			if (s)
				os << ' ';
			if (slot.kind == SlotKind::Keyword)
			{
				os << slot.name;
			}
			else
			{
				os << '<' << slot.name << (slot.variadic ? "..." : "") << '>';
			}
		}
		bool anyFlagOrOption =
			std::any_of(m_unstructured.begin(), m_unstructured.end(),
						[](const Slot& s) { return s.kind != SlotKind::Parameter; });
		if (anyFlagOrOption)
			os << " [OPTIONS]";
		for (const Slot& slot : m_unstructured)
		{
			if (slot.kind != SlotKind::Parameter)
				continue;
			os << " <" << slot.name << (slot.variadic ? "..." : "") << '>';
		}
		return os.str();
	}

	std::string Command::helpText(const std::string& progName) const
	{
		std::ostringstream os;
		os << "Usage: " << progName << " " << usageLine() << "\n";
		if (!m_description.empty())
			os << "\n" << m_description << "\n";

		bool anyParams = std::any_of(m_structured.begin(), m_structured.end(),
									 [](const Slot& s) { return s.kind == SlotKind::Parameter; }) ||
						 std::any_of(m_unstructured.begin(), m_unstructured.end(),
									 [](const Slot& s) { return s.kind == SlotKind::Parameter; });
		if (anyParams)
		{
			os << "\nArguments:\n";
			for (const Slot& slot : m_structured)
			{
				if (slot.kind != SlotKind::Parameter)
					continue;
				std::string label = "<" + slot.name + (slot.variadic ? "..." : "") + ">";
				os << "  " << padRight(label, 22) << describeType(slot) << slot.description << "\n";
			}
			for (const Slot& slot : m_unstructured)
			{
				if (slot.kind != SlotKind::Parameter)
					continue;
				std::string label = "<" + slot.name + (slot.variadic ? "..." : "") + ">";
				os << "  " << padRight(label, 22) << describeType(slot) << slot.description << "\n";
			}
		}

		bool anyFlagOrOption =
			std::any_of(m_unstructured.begin(), m_unstructured.end(),
						[](const Slot& s) { return s.kind != SlotKind::Parameter; });
		os << "\nOptions:\n";
		if (anyFlagOrOption)
		{
			for (const Slot& slot : m_unstructured)
			{
				if (slot.kind == SlotKind::Parameter)
					continue;
				std::string names = slot.shortName == '\0'
										? ("    --" + slot.name)
										: (std::string("-") + slot.shortName + ", --" + slot.name);
				if (slot.kind == SlotKind::Option)
				{
					names += " <" + slot.type.displayName + (slot.variadic ? "...>" : ">");
				}
				os << "  " << padRight(names, 28) << slot.description << "\n";
			}
		}
		os << "  " << padRight("-h, --help", 28) << "Show this help message\n";
		return os.str();
	}

	std::vector<std::string> Command::flagOptionNames() const
	{
		std::vector<std::string> out;
		for (const Slot& s : m_unstructured)
		{
			if (s.kind == SlotKind::Parameter)
				continue;
			out.push_back("--" + s.name);
			if (s.shortName != '\0')
				out.push_back(std::string("-") + s.shortName);
		}
		return out;
	}

	Command::FuzzyScore Command::fuzzyStructuralDistance(
		const std::vector<std::string>& tokens) const
	{
		std::size_t i = 0;
		FuzzyScore score;
		for (std::size_t s = 0; s < m_structured.size(); ++s)
		{
			const Slot& slot = m_structured[s];
			bool isLast = (s + 1 == m_structured.size());
			if (slot.kind == SlotKind::Keyword)
			{
				score.keywordChars += slot.name.size();
				if (i >= tokens.size())
				{
					score.distance += slot.name.size(); // command is longer than what was typed
					continue;
				}
				score.distance += detail::levenshtein(tokens[i], slot.name);
				++i;
			}
			else if (slot.variadic && isLast)
			{
				while (i < tokens.size() && !detail::looksLikeOptionStart(tokens[i]))
					++i;
			}
			else if (i < tokens.size() && !detail::looksLikeOptionStart(tokens[i]))
			{
				++i; // parameters are wildcards: any token "fits", no penalty
			}
		}
		score.distance += (tokens.size() > i) ? (tokens.size() - i) : 0; // leftover tokens
		return score;
	}

	std::string Command::stripDashes(std::string s)
	{
		while (!s.empty() && s.front() == '-')
			s.erase(s.begin());
		return s;
	}

	void Command::validateLongName(const std::string& s) const
	{
		if (s.empty())
			throw RegistrationError("a flag/option needs a non-empty long name");
		if (s == "help")
		{
			throw RegistrationError("'help' is reserved for the built-in --help flag");
		}
		for (char c : s)
		{
			if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_'))
			{
				throw RegistrationError("invalid long name '--" + s + "'");
			}
		}
	}

	void Command::validateShortName(char c) const
	{
		if (c == '\0')
			return; // no short form
		if (!std::isalnum(static_cast<unsigned char>(c)))
		{
			throw RegistrationError(
				std::string("short names must be a single letter/digit, got '-") + c + "'");
		}
		if (c == 'h')
			throw RegistrationError("'-h' is reserved for the built-in --help flag");
	}

	void Command::checkNameFree(const std::string& longName, char shortName) const
	{
		for (const Slot& s : m_unstructured)
		{
			if (s.kind == SlotKind::Parameter)
				continue; // different namespace (bare, not "--name")
			if (s.name == longName)
			{
				throw RegistrationError("duplicate long name '--" + longName + "'");
			}
			if (shortName != '\0' && s.shortName == shortName)
			{
				throw RegistrationError(std::string("duplicate short name '-") + shortName + "'");
			}
		}
	}

	void Command::checkNotAwaitingChoices(const Slot& s)
	{
		if (s.awaitingChoices)
		{
			throw RegistrationError("'" + s.name +
									"' is a custom enum type and needs choices<T>(...) "
									"called right after it is declared");
		}
	}

	std::vector<Slot*> Command::orderedBindableSlots()
	{
		std::vector<Slot*> out;
		for (Slot& s : m_structured)
		{
			if (s.kind == SlotKind::Parameter)
				out.push_back(&s);
		}
		for (Slot& s : m_unstructured)
			out.push_back(&s);
		return out;
	}

	int Command::findByLong(std::string_view name) const
	{
		for (std::size_t i = 0; i < m_unstructured.size(); ++i)
		{
			if (m_unstructured[i].kind != SlotKind::Parameter && m_unstructured[i].name == name)
			{
				return static_cast<int>(i);
			}
		}
		return -1;
	}

	int Command::findByShort(char c) const
	{
		if (c == '\0')
			return -1;
		for (std::size_t i = 0; i < m_unstructured.size(); ++i)
		{
			if (m_unstructured[i].kind != SlotKind::Parameter && m_unstructured[i].shortName == c)
			{
				return static_cast<int>(i);
			}
		}
		return -1;
	}

	std::string Command::suggestFlagOption(const std::string& given) const
	{
		auto names = flagOptionNames();
		names.push_back("--help");
		names.push_back("-h");
		auto close = detail::closestMatches(given, names, 1);
		if (close.empty())
			return "";
		return " -- did you mean '" + close.front() + "'?";
	}

	[[noreturn]] void Command::throwUnknown(std::string_view tok) const
	{
		throw CliError("unknown flag/option '" + std::string(tok) + "' for this command" +
						   suggestFlagOption(std::string(tok)),
					   ErrorKind::UnknownOption);
	}

	std::string Command::describeType(const Slot& slot)
	{
		return "<" + slot.type.displayName + "> ";
	}

	std::string Command::padRight(const std::string& s, std::size_t width)
	{
		if (s.size() >= width)
			return s + " ";
		return s + std::string(width - s.size(), ' ');
	}

	void Command::consumeSlot(const Command& self, int idxIn, bool hasInline,
							  std::string_view inlineValue, const std::vector<std::string>& tokens,
							  std::size_t& i, std::vector<bool>& provided,
							  std::vector<std::vector<Scalar>>& collected,
							  std::string_view tokForError)
	{
		std::size_t idx = static_cast<std::size_t>(idxIn);
		const Slot& slot = self.m_unstructured[idx];
		if (slot.kind == SlotKind::Flag)
		{
			if (hasInline)
				throw CliError("flag '--" + slot.name + "' does not take a value");
			provided[idx] = true;
			return;
		}
		// Option
		if (hasInline)
		{
			collected[idx].push_back(slot.type.parse(inlineValue, slot.name));
			provided[idx] = true;
			if (slot.variadic)
			{
				while (i < tokens.size() && !detail::looksLikeOptionStart(tokens[i]))
				{
					collected[idx].push_back(slot.type.parse(tokens[i], slot.name));
					++i;
				}
			}
			return;
		}
		if (slot.variadic)
		{
			std::size_t before = i;
			while (i < tokens.size() && !detail::looksLikeOptionStart(tokens[i]))
			{
				collected[idx].push_back(slot.type.parse(tokens[i], slot.name));
				++i;
			}
			if (i == before)
			{
				throw CliError("option '" + std::string(tokForError) +
								   "' requires at least one value",
							   ErrorKind::MissingValue);
			}
			provided[idx] = true;
		}
		else
		{
			if (i >= tokens.size() || detail::looksLikeOptionStart(tokens[i]))
			{
				throw CliError("option '" + std::string(tokForError) + "' requires a value",
							   ErrorKind::MissingValue);
			}
			collected[idx] = {slot.type.parse(tokens[i], slot.name)};
			provided[idx] = true;
			++i;
		}
	};
}
