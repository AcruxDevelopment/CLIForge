#pragma once
//
// command.hpp
//
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
//
#include <algorithm>
#include <cctype>
#include <functional>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <typeindex>
#include <vector>

#include "function_traits.hpp"
#include "levenshtein.hpp"
#include "slot.hpp"
#include "value.hpp"

namespace cliforge {

namespace detail {

// --- small type-trait helpers used only for action() binding ----------

template <typename T>
struct is_optional : std::false_type {};
template <typename T>
struct is_optional<std::optional<T>> : std::true_type {
    using inner = T;
};
template <typename T>
inline constexpr bool is_optional_v = is_optional<T>::value;

template <typename T>
struct is_vector : std::false_type {};
template <typename T>
struct is_vector<std::vector<T>> : std::true_type {
    using inner = T;
};
template <typename T>
inline constexpr bool is_vector_v = is_vector<T>::value;

template <typename T>
inline constexpr bool is_builtin_scalar_v =
    std::is_same_v<T, int8_t> || std::is_same_v<T, uint8_t> ||
    std::is_same_v<T, int16_t> || std::is_same_v<T, uint16_t> ||
    std::is_same_v<T, int32_t> || std::is_same_v<T, uint32_t> ||
    std::is_same_v<T, int64_t> || std::is_same_v<T, uint64_t> ||
    std::is_same_v<T, float> || std::is_same_v<T, double> ||
    std::is_same_v<T, std::string> || std::is_same_v<T, char> ||
    std::is_same_v<T, bool>;

// A token is where the unstructured (flags/options) part begins if it
// starts with a dash and isn't shaped like a negative number -- that way
// `build -5 -3 -1` (variadic int parameters) still works even though the
// tokens start with '-'.
inline bool looksLikeOptionStart(std::string_view tok) {
    if (tok.size() < 2 || tok[0] != '-') return false;
    if (tok[1] == '-') return true;               // "--foo"
    if (std::isdigit(static_cast<unsigned char>(tok[1]))) return false;  // "-5"
    return true;                                   // "-n", "-ndi"
}

inline bool isValidKeyword(std::string_view s) {
    if (s.empty()) return false;
    if (s[0] == '-') return false;  // reserved for flags/options
    for (char c : s) {
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '.' ||
              c == '_' || c == '-')) {
            return false;
        }
    }
    return true;
}

// A human-readable name for a bound action() argument type, used in
// RegistrationError messages. Only built-in scalars and enums are ever
// legal here (anything else is already rejected by parameter<T>()'s own
// static_assert), so this never needs to handle the general case.
template <typename T>
std::string friendlyTypeName() {
    if constexpr (is_builtin_scalar_v<T>) {
        return TypeOf<T>::get().displayName;
    } else if constexpr (std::is_enum_v<T>) {
        return "a different enum type";
    } else {
        return "an unsupported type";
    }
}

}  // namespace detail

// Result of trying to line a command's structured (keyword+parameter)
// signature up against the front of the user's token list.
struct StructuredMatch {
    bool success = false;
    std::size_t consumed = 0;      // tokens belonging to the structured part
    int keywordScore = 0;          // number of literal keywords matched
    std::vector<Value> paramValues;
    // Set when every literal keyword matched but a parameter's value
    // either failed to parse or was never given -- lets the Engine
    // surface this specific, actionable error instead of a generic
    // "no match" + fuzzy suggestions.
    std::string error;
};

class Command {
public:
    // Keywords carry no description of their own -- .describe() on the
    // command is what shows up in help; a keyword is purely a literal
    // token to match.
    Command& keyword(std::string literal) {
        if (unstructuredStarted_) {
            throw RegistrationError(
                "can't declare keyword('" + literal +
                "') after a flag()/option() was added -- keywords must "
                "come entirely before the unstructured part");
        }
        if (structuredSealed_) {
            throw RegistrationError(
                "can't declare keyword('" + literal +
                "') after a variadic parameter -- a variadic parameter "
                "must be the last thing in the structured part");
        }
        if (!detail::isValidKeyword(literal)) {
            throw RegistrationError(
                "invalid keyword '" + literal +
                "': keywords may contain letters, digits, '.', '_' and "
                "'-', and may not start with '-'");
        }
        Slot s;
        s.kind = SlotKind::Keyword;
        s.name = std::move(literal);
        structured_.push_back(std::move(s));
        lastSlot_ = &structured_.back();
        return *this;
    }

    // T is explicit and required: every parameter is typed at the point
    // it's declared, not inferred later from action(). Called before any
    // flag()/option(), this is a fixed-position structured parameter;
    // called after, it's a "loose" parameter matched positionally among
    // other loose parameters regardless of where flags/options fall
    // around it.
    template <typename T>
    Command& parameter(std::string name, std::string description, bool variadic = false) {
        if (name.empty()) {
            throw RegistrationError("parameter name may not be empty");
        }
        Slot s;
        s.kind = SlotKind::Parameter;
        s.name = std::move(name);
        s.description = std::move(description);
        s.variadic = variadic;
        s.cppType = std::type_index(typeid(T));
        if constexpr (std::is_enum_v<T>) {
            s.awaitingChoices = true;  // choices<T>(...) must follow immediately
        } else {
            static_assert(detail::is_builtin_scalar_v<T>,
                          "cliforge: unsupported parameter type -- use a fixed-width int, "
                          "float/double, std::string, char, bool, or an enum (with "
                          "choices<T>() called immediately after)");
            s.type = TypeOf<T>::get();
        }
        if (!unstructuredStarted_) {
            if (structuredSealed_) {
                throw RegistrationError(
                    "can't declare parameter('" + s.name +
                    "') after a variadic parameter -- a variadic parameter "
                    "must be the last thing in the structured part");
            }
            structured_.push_back(std::move(s));
            lastSlot_ = &structured_.back();
            if (variadic) structuredSealed_ = true;
        } else {
            if (looseSealed_) {
                throw RegistrationError(
                    "can't declare parameter('" + s.name +
                    "') after a variadic parameter -- a variadic parameter "
                    "must be the last parameter declared");
            }
            unstructured_.push_back(std::move(s));
            lastSlot_ = &unstructured_.back();
            if (variadic) looseSealed_ = true;
        }
        return *this;
    }

    // shortName is 0 (the default) for "no short form".
    Command& flag(std::string longName, char shortName, std::string description) {
        longName = stripDashes(std::move(longName));
        validateLongName(longName);
        validateShortName(shortName);
        checkNameFree(longName, shortName);
        Slot s;
        s.kind = SlotKind::Flag;
        s.name = std::move(longName);
        s.shortName = shortName;
        s.description = std::move(description);
        unstructured_.push_back(std::move(s));
        lastSlot_ = &unstructured_.back();
        unstructuredStarted_ = true;
        return *this;
    }

    // T is explicit and required, same as parameter<T>(). Options are
    // always optional in the calling convention (see action()) -- there
    // is no way to declare a "required" option, by design.
    template <typename T>
    Command& option(std::string longName, char shortName, std::string description,
                     bool variadic = false) {
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
        if constexpr (std::is_enum_v<T>) {
            s.awaitingChoices = true;
        } else {
            static_assert(detail::is_builtin_scalar_v<T>,
                          "cliforge: unsupported option type -- use a fixed-width int, "
                          "float/double, std::string, char, bool, or an enum (with "
                          "choices<T>() called immediately after)");
            s.type = TypeOf<T>::get();
        }
        unstructured_.push_back(std::move(s));
        lastSlot_ = &unstructured_.back();
        unstructuredStarted_ = true;
        return *this;
    }

    Command& describe(std::string description) {
        description_ = std::move(description);
        return *this;
    }

    // Restricts the most-recently-declared parameter/option to a fixed
    // set of named values -- this is how custom enums plug in (T is
    // deduced from the initializer list, no need to spell it out again),
    // and it doubles as a generic "choice" restriction for any type. T
    // must match the type declared at parameter<T>()/option<T>().
    template <typename T>
    Command& choices(std::initializer_list<std::pair<std::string, T>> values,
                      std::string typeName = "") {
        if (lastSlot_ == nullptr ||
            (lastSlot_->kind != SlotKind::Parameter && lastSlot_->kind != SlotKind::Option)) {
            throw RegistrationError(
                "choices<>() must directly follow the parameter<T>() or "
                "option<T>() it restricts");
        }
        if (!lastSlot_->cppType || *lastSlot_->cppType != std::type_index(typeid(T))) {
            throw RegistrationError("choices<>() type doesn't match the type declared for '" +
                                     lastSlot_->name + "'");
        }
        std::vector<std::pair<std::string, T>> table(values);
        lastSlot_->type =
            makeChoiceTypeInfo<T>(typeName.empty() ? std::string("enum") : std::move(typeName), table);
        lastSlot_->awaitingChoices = false;
        return *this;
    }

    // Binds this command to a free function (or captureless/capturing
    // lambda, or std::function). Argument types are validated against
    // what was already declared via parameter<T>()/option<T>()/flag() --
    // action() never invents a type, it only checks consistency -- and
    // matched, in order, to [parameters in declaration order] then
    // [flags/options/loose-parameters in declaration order]. Arguments
    // may be taken by value, by reference, or by const reference.
    template <typename Func>
    Command& action(Func f) {
        using Traits = detail::function_traits<std::decay_t<Func>>;
        if (structured_.empty() ||
            std::none_of(structured_.begin(), structured_.end(), [](const Slot& s) {
                return s.kind == SlotKind::Keyword;
            })) {
            throw RegistrationError(
                "a command must declare at least one keyword() so it can "
                "be identified on the command line");
        }
        for (const Slot& s : structured_) checkNotAwaitingChoices(s);
        for (const Slot& s : unstructured_) checkNotAwaitingChoices(s);

        std::vector<Slot*> ordered = orderedBindableSlots();
        if (ordered.size() != Traits::arity) {
            throw RegistrationError(
                "action() function takes " + std::to_string(Traits::arity) +
                " argument(s) but this command declared " +
                std::to_string(ordered.size()) +
                " parameter/flag/option slot(s) -- these must match 1:1, "
                "in declaration order (parameters first, then flags/options)");
        }
        bindArgs(std::function{f}, ordered, std::make_index_sequence<Traits::arity>{});
        sealed_ = true;
        return *this;
    }

    bool sealed() const { return sealed_; }
    const std::string& description() const { return description_; }

    // --- matching / parsing entry points, used by Engine -------------

    StructuredMatch tryMatchStructured(const std::vector<std::string>& tokens) const {
        StructuredMatch m;
        std::size_t i = 0;
        try {
            for (std::size_t s = 0; s < structured_.size(); ++s) {
                const Slot& slot = structured_[s];
                bool isLast = (s + 1 == structured_.size());
                if (slot.kind == SlotKind::Keyword) {
                    if (i >= tokens.size() || tokens[i] != slot.name) return StructuredMatch{};
                    ++i;
                    ++m.keywordScore;
                } else {
                    if (slot.variadic && isLast) {
                        std::vector<Scalar> collected;
                        while (i < tokens.size() && !detail::looksLikeOptionStart(tokens[i])) {
                            collected.push_back(slot.type.parse(tokens[i], slot.name));
                            ++i;
                        }
                        m.paramValues.push_back(Value::ofVector(std::move(collected)));
                    } else {
                        if (i >= tokens.size() || detail::looksLikeOptionStart(tokens[i])) {
                            // Every keyword up to here matched literally, so
                            // this is almost certainly the intended command
                            // -- just missing a required argument.
                            throw CliError(
                                "missing required argument '<" + slot.name +
                                    ">': expected a value of type " + slot.type.displayName,
                                ErrorKind::MissingArgument);
                        }
                        m.paramValues.push_back(Value::ofScalar(slot.type.parse(tokens[i], slot.name)));
                        ++i;
                    }
                }
            }
        } catch (const CliError& e) {
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
    std::vector<Value> parseUnstructured(const std::vector<std::string>& tokens) const {
        std::vector<bool> provided(unstructured_.size(), false);
        std::vector<std::vector<Scalar>> collected(unstructured_.size());
        std::vector<std::string_view> looseTokens;  // views into `tokens`, which outlives this call
        looseTokens.reserve(tokens.size());

        std::size_t i = 0;
        while (i < tokens.size()) {
            std::string_view tok = tokens[i];
            if (!detail::looksLikeOptionStart(tok)) {
                // Not flag/option syntax (includes negative numbers like
                // "-5") -- it's a value for the next loose parameter.
                looseTokens.push_back(tok);
                ++i;
                continue;
            }
            if (tok[1] == '-') {
                std::string_view rest = tok.substr(2);
                auto eq = rest.find('=');
                bool hasInline = (eq != std::string_view::npos);
                std::string_view longName = hasInline ? rest.substr(0, eq) : rest;
                std::string_view inlineValue = hasInline ? rest.substr(eq + 1) : std::string_view{};
                int idx = findByLong(longName);
                if (idx < 0) throwUnknown(tok);
                ++i;
                consumeSlot(*this, idx, hasInline, inlineValue, tokens, i, provided, collected, tok);
            } else {
                std::string_view rest = tok.substr(1);
                if (rest.size() == 1) {
                    int idx = findByShort(rest[0]);
                    if (idx < 0) throwUnknown(tok);
                    ++i;
                    consumeSlot(*this, idx, false, std::string_view{}, tokens, i, provided, collected, tok);
                } else {
                    // Combined short flags, e.g. "-ndi" == -n -d -i.
                    for (char c : rest) {
                        int idx = findByShort(c);
                        if (idx < 0) {
                            throw CliError(std::string("unknown flag '-") + c +
                                                "' in combined group '" + std::string(tok) + "'" +
                                                suggestFlagOption(std::string("-") + c),
                                            ErrorKind::UnknownOption);
                        }
                        if (unstructured_[static_cast<size_t>(idx)].kind != SlotKind::Flag) {
                            throw CliError(
                                std::string("'-") + c + "' in combined group '" + std::string(tok) +
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
        for (std::size_t idx = 0; idx < unstructured_.size(); ++idx) {
            if (unstructured_[idx].kind == SlotKind::Parameter) looseParamIdx.push_back(idx);
        }
        std::vector<Value> looseValues(looseParamIdx.size());
        std::size_t li = 0;
        for (std::size_t k = 0; k < looseParamIdx.size(); ++k) {
            const Slot& slot = unstructured_[looseParamIdx[k]];
            bool isLastLoose = (k + 1 == looseParamIdx.size());
            if (slot.variadic && isLastLoose) {
                std::vector<Scalar> vals;
                vals.reserve(looseTokens.size() > li ? looseTokens.size() - li : 0);
                while (li < looseTokens.size()) {
                    vals.push_back(slot.type.parse(looseTokens[li], slot.name));
                    ++li;
                }
                looseValues[k] = Value::ofVector(std::move(vals));
            } else {
                if (li >= looseTokens.size()) {
                    throw CliError("missing required argument '<" + slot.name +
                                        ">': expected a value of type " + slot.type.displayName,
                                    ErrorKind::MissingArgument);
                }
                looseValues[k] = Value::ofScalar(slot.type.parse(looseTokens[li], slot.name));
                ++li;
            }
        }
        if (li < looseTokens.size()) {
            std::size_t extra = looseTokens.size() - li;
            std::string msg = "too many arguments: unexpected '" + std::string(looseTokens[li]) + "'";
            if (extra > 1) msg += " (and " + std::to_string(extra - 1) + " more)";
            throw CliError(msg, ErrorKind::TooManyArguments);
        }

        // Assemble the final values in unstructured_'s declaration order --
        // this is what gets paired with the bound function's arguments.
        std::vector<Value> out;
        out.reserve(unstructured_.size());
        std::size_t looseCursor = 0;
        for (std::size_t idx = 0; idx < unstructured_.size(); ++idx) {
            const Slot& slot = unstructured_[idx];
            if (slot.kind == SlotKind::Parameter) {
                out.push_back(std::move(looseValues[looseCursor++]));
            } else if (slot.kind == SlotKind::Flag) {
                out.push_back(Value::ofScalar(Scalar{provided[idx]}));
            } else if (slot.variadic) {
                out.push_back(Value::ofVector(collected[idx]));
            } else if (provided[idx]) {
                out.push_back(Value::ofScalar(collected[idx][0]));
            } else {
                out.push_back(Value{});  // options are always optional -> unset means nullopt
            }
        }
        return out;
    }

    void invoke(std::vector<Value> allValues) const { invoke_(allValues); }

    std::size_t structuredSlotCount() const { return structured_.size(); }

    // Lenient prefix check used only for --help / `help <words>` : does
    // `tokens` look like the start of (or a complete instance of) this
    // command, without requiring every required argument to actually be
    // present? Keywords still must match exactly; parameter slots accept
    // any token; running out of input mid-command is fine (that's exactly
    // what makes it a "prefix"). Used to find *every* command consistent
    // with what's been typed so far, so an ambiguous prefix can show help
    // for all of them instead of guessing one.
    bool couldMatchPrefix(const std::vector<std::string>& tokens) const {
        std::size_t i = 0;
        for (std::size_t s = 0; s < structured_.size(); ++s) {
            if (i >= tokens.size()) break;  // ran out of input -- a valid, incomplete prefix
            const Slot& slot = structured_[s];
            bool isLast = (s + 1 == structured_.size());
            if (slot.kind == SlotKind::Keyword) {
                if (tokens[i] != slot.name) return false;
                ++i;
            } else if (slot.variadic && isLast) {
                while (i < tokens.size() && !detail::looksLikeOptionStart(tokens[i])) ++i;
            } else {
                if (detail::looksLikeOptionStart(tokens[i])) break;  // structured part left incomplete
                ++i;
            }
        }
        // Whatever tokens remain must be plausible flags/options (assumed
        // fine without deep validation) or fit a declared loose parameter.
        std::vector<const Slot*> looseParams;
        for (const Slot& s : unstructured_) {
            if (s.kind == SlotKind::Parameter) looseParams.push_back(&s);
        }
        std::size_t looseIdx = 0;
        for (; i < tokens.size(); ++i) {
            if (detail::looksLikeOptionStart(tokens[i])) continue;
            if (looseIdx >= looseParams.size()) return false;  // nothing left to absorb this token
            if (!looseParams[looseIdx]->variadic) ++looseIdx;
        }
        return true;
    }

    // --- help / usage --------------------------------------------------

    std::string usageLine() const {
        std::ostringstream os;
        for (std::size_t s = 0; s < structured_.size(); ++s) {
            const Slot& slot = structured_[s];
            if (s) os << ' ';
            if (slot.kind == SlotKind::Keyword) {
                os << slot.name;
            } else {
                os << '<' << slot.name << (slot.variadic ? "..." : "") << '>';
            }
        }
        bool anyFlagOrOption = std::any_of(unstructured_.begin(), unstructured_.end(),
                                            [](const Slot& s) { return s.kind != SlotKind::Parameter; });
        if (anyFlagOrOption) os << " [OPTIONS]";
        for (const Slot& slot : unstructured_) {
            if (slot.kind != SlotKind::Parameter) continue;
            os << " <" << slot.name << (slot.variadic ? "..." : "") << '>';
        }
        return os.str();
    }

    std::string helpText(const std::string& progName) const {
        std::ostringstream os;
        os << "Usage: " << progName << " " << usageLine() << "\n";
        if (!description_.empty()) os << "\n" << description_ << "\n";

        bool anyParams = std::any_of(structured_.begin(), structured_.end(),
                                      [](const Slot& s) { return s.kind == SlotKind::Parameter; }) ||
                          std::any_of(unstructured_.begin(), unstructured_.end(),
                                      [](const Slot& s) { return s.kind == SlotKind::Parameter; });
        if (anyParams) {
            os << "\nArguments:\n";
            for (const Slot& slot : structured_) {
                if (slot.kind != SlotKind::Parameter) continue;
                std::string label = "<" + slot.name + (slot.variadic ? "..." : "") + ">";
                os << "  " << padRight(label, 22) << describeType(slot) << slot.description << "\n";
            }
            for (const Slot& slot : unstructured_) {
                if (slot.kind != SlotKind::Parameter) continue;
                std::string label = "<" + slot.name + (slot.variadic ? "..." : "") + ">";
                os << "  " << padRight(label, 22) << describeType(slot) << slot.description << "\n";
            }
        }

        bool anyFlagOrOption = std::any_of(unstructured_.begin(), unstructured_.end(),
                                            [](const Slot& s) { return s.kind != SlotKind::Parameter; });
        os << "\nOptions:\n";
        if (anyFlagOrOption) {
            for (const Slot& slot : unstructured_) {
                if (slot.kind == SlotKind::Parameter) continue;
                std::string names = slot.shortName == '\0'
                                         ? ("    --" + slot.name)
                                         : (std::string("-") + slot.shortName + ", --" + slot.name);
                if (slot.kind == SlotKind::Option) {
                    names += " <" + slot.type.displayName + (slot.variadic ? "...>" : ">");
                }
                os << "  " << padRight(names, 28) << slot.description << "\n";
            }
        }
        os << "  " << padRight("-h, --help", 28) << "Show this help message\n";
        return os.str();
    }

    std::vector<std::string> flagOptionNames() const {
        std::vector<std::string> out;
        for (const Slot& s : unstructured_) {
            if (s.kind == SlotKind::Parameter) continue;
            out.push_back("--" + s.name);
            if (s.shortName != '\0') out.push_back(std::string("-") + s.shortName);
        }
        return out;
    }

    // Fuzzy structural distance used for "did you mean...?" suggestions.
    struct FuzzyScore {
        std::size_t distance = 0;
        std::size_t keywordChars = 0;  // denominator for a relative threshold
    };

    FuzzyScore fuzzyStructuralDistance(const std::vector<std::string>& tokens) const {
        std::size_t i = 0;
        FuzzyScore score;
        for (std::size_t s = 0; s < structured_.size(); ++s) {
            const Slot& slot = structured_[s];
            bool isLast = (s + 1 == structured_.size());
            if (slot.kind == SlotKind::Keyword) {
                score.keywordChars += slot.name.size();
                if (i >= tokens.size()) {
                    score.distance += slot.name.size();  // command is longer than what was typed
                    continue;
                }
                score.distance += detail::levenshtein(tokens[i], slot.name);
                ++i;
            } else if (slot.variadic && isLast) {
                while (i < tokens.size() && !detail::looksLikeOptionStart(tokens[i])) ++i;
            } else if (i < tokens.size() && !detail::looksLikeOptionStart(tokens[i])) {
                ++i;  // parameters are wildcards: any token "fits", no penalty
            }
        }
        score.distance += (tokens.size() > i) ? (tokens.size() - i) : 0;  // leftover tokens
        return score;
    }

private:
    std::vector<Slot> structured_;
    std::vector<Slot> unstructured_;
    std::string description_;
    std::function<void(std::vector<Value>&)> invoke_;
    Slot* lastSlot_ = nullptr;
    bool unstructuredStarted_ = false;
    bool structuredSealed_ = false;
    bool looseSealed_ = false;
    bool sealed_ = false;

    static std::string stripDashes(std::string s) {
        while (!s.empty() && s.front() == '-') s.erase(s.begin());
        return s;
    }

    void validateLongName(const std::string& s) const {
        if (s.empty()) throw RegistrationError("a flag/option needs a non-empty long name");
        if (s == "help") {
            throw RegistrationError("'help' is reserved for the built-in --help flag");
        }
        for (char c : s) {
            if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_')) {
                throw RegistrationError("invalid long name '--" + s + "'");
            }
        }
    }

    void validateShortName(char c) const {
        if (c == '\0') return;  // no short form
        if (!std::isalnum(static_cast<unsigned char>(c))) {
            throw RegistrationError(std::string("short names must be a single letter/digit, got '-") +
                                     c + "'");
        }
        if (c == 'h') throw RegistrationError("'-h' is reserved for the built-in --help flag");
    }

    void checkNameFree(const std::string& longName, char shortName) const {
        for (const Slot& s : unstructured_) {
            if (s.kind == SlotKind::Parameter) continue;  // different namespace (bare, not "--name")
            if (s.name == longName) {
                throw RegistrationError("duplicate long name '--" + longName + "'");
            }
            if (shortName != '\0' && s.shortName == shortName) {
                throw RegistrationError(std::string("duplicate short name '-") + shortName + "'");
            }
        }
    }

    static void checkNotAwaitingChoices(const Slot& s) {
        if (s.awaitingChoices) {
            throw RegistrationError("'" + s.name +
                                     "' is a custom enum type and needs choices<T>(...) "
                                     "called right after it is declared");
        }
    }

    std::vector<Slot*> orderedBindableSlots() {
        std::vector<Slot*> out;
        for (Slot& s : structured_) {
            if (s.kind == SlotKind::Parameter) out.push_back(&s);
        }
        for (Slot& s : unstructured_) out.push_back(&s);
        return out;
    }

    // Validates (never invents) that a slot's declared type matches T.
    template <typename T>
    static void checkTypeMatches(const Slot& slot) {
        if (!slot.cppType || *slot.cppType != std::type_index(typeid(T))) {
            throw RegistrationError("'" + slot.name + "' was declared as " + slot.type.displayName +
                                     " but action() expects " + detail::friendlyTypeName<T>());
        }
    }

    // ArgT is the *raw* action() parameter type, e.g. `const std::string&`
    // or `std::vector<int32_t>` -- validated after stripping cv/ref.
    template <typename ArgT>
    static void configureSlot(Slot& slot) {
        using Stripped = std::remove_cvref_t<ArgT>;
        if (slot.kind == SlotKind::Flag) {
            if constexpr (!std::is_same_v<Stripped, bool>) {
                throw RegistrationError("flag '--" + slot.name + "' must bind to bool in action()");
            }
            return;
        }
        if constexpr (detail::is_optional_v<Stripped>) {
            using Inner = typename detail::is_optional<Stripped>::inner;
            if (slot.kind == SlotKind::Parameter) {
                throw RegistrationError("parameter '<" + slot.name +
                                         ">' can't bind to std::optional<> -- positional "
                                         "parameters are always required");
            }
            if (slot.variadic) {
                throw RegistrationError("option '--" + slot.name +
                                         "' is variadic; bind it to std::vector<>, not std::optional<>");
            }
            checkTypeMatches<Inner>(slot);
        } else if constexpr (detail::is_vector_v<Stripped>) {
            using Inner = typename detail::is_vector<Stripped>::inner;
            if (!slot.variadic) {
                throw RegistrationError("'" + slot.name +
                                         "' binds to std::vector<> but wasn't declared variadic=true");
            }
            checkTypeMatches<Inner>(slot);
        } else {
            if (slot.variadic) {
                throw RegistrationError("'" + slot.name +
                                         "' was declared variadic=true; bind it to std::vector<>");
            }
            if (slot.kind == SlotKind::Option) {
                throw RegistrationError("option '--" + slot.name +
                                         "' must bind to std::optional<T> (or std::vector<T> if "
                                         "variadic) -- options are always optional, never required");
            }
            checkTypeMatches<Stripped>(slot);
        }
    }

    template <typename ArgT>
    static ArgT extractArg(const Value& v) {
        if constexpr (std::is_same_v<ArgT, bool>) {
            return v.as<bool>();
        } else if constexpr (detail::is_optional_v<ArgT>) {
            using Inner = typename detail::is_optional<ArgT>::inner;
            return v.hasValue() ? std::optional<Inner>(v.as<Inner>()) : std::nullopt;
        } else if constexpr (detail::is_vector_v<ArgT>) {
            using Inner = typename detail::is_vector<ArgT>::inner;
            return v.asVector<Inner>();
        } else {
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
                  std::index_sequence<I...>) {
        (configureSlot<Args>(*ordered[I]), ...);
        invoke_ = [f = std::move(f)](std::vector<Value>& values) {
            std::tuple<std::remove_cvref_t<Args>...> extracted{
                extractArg<std::remove_cvref_t<Args>>(values[I])...};
            std::apply(f, extracted);
        };
    }

    int findByLong(std::string_view name) const {
        for (std::size_t i = 0; i < unstructured_.size(); ++i) {
            if (unstructured_[i].kind != SlotKind::Parameter && unstructured_[i].name == name) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }
    int findByShort(char c) const {
        if (c == '\0') return -1;
        for (std::size_t i = 0; i < unstructured_.size(); ++i) {
            if (unstructured_[i].kind != SlotKind::Parameter && unstructured_[i].shortName == c) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    std::string suggestFlagOption(const std::string& given) const {
        auto names = flagOptionNames();
        names.push_back("--help");
        names.push_back("-h");
        auto close = detail::closestMatches(given, names, 1);
        if (close.empty()) return "";
        return " -- did you mean '" + close.front() + "'?";
    }

    [[noreturn]] void throwUnknown(std::string_view tok) const {
        throw CliError("unknown flag/option '" + std::string(tok) + "' for this command" +
                            suggestFlagOption(std::string(tok)),
                        ErrorKind::UnknownOption);
    }

    static std::string describeType(const Slot& slot) {
        return "<" + slot.type.displayName + "> ";
    }

    static std::string padRight(const std::string& s, std::size_t width) {
        if (s.size() >= width) return s + " ";
        return s + std::string(width - s.size(), ' ');
    }

    static void consumeSlot(const Command& self, int idxIn, bool hasInline,
                             std::string_view inlineValue, const std::vector<std::string>& tokens,
                             std::size_t& i, std::vector<bool>& provided,
                             std::vector<std::vector<Scalar>>& collected, std::string_view tokForError) {
        std::size_t idx = static_cast<std::size_t>(idxIn);
        const Slot& slot = self.unstructured_[idx];
        if (slot.kind == SlotKind::Flag) {
            if (hasInline) throw CliError("flag '--" + slot.name + "' does not take a value");
            provided[idx] = true;
            return;
        }
        // Option
        if (hasInline) {
            collected[idx].push_back(slot.type.parse(inlineValue, slot.name));
            provided[idx] = true;
            if (slot.variadic) {
                while (i < tokens.size() && !detail::looksLikeOptionStart(tokens[i])) {
                    collected[idx].push_back(slot.type.parse(tokens[i], slot.name));
                    ++i;
                }
            }
            return;
        }
        if (slot.variadic) {
            std::size_t before = i;
            while (i < tokens.size() && !detail::looksLikeOptionStart(tokens[i])) {
                collected[idx].push_back(slot.type.parse(tokens[i], slot.name));
                ++i;
            }
            if (i == before) {
                throw CliError("option '" + std::string(tokForError) + "' requires at least one value",
                                ErrorKind::MissingValue);
            }
            provided[idx] = true;
        } else {
            if (i >= tokens.size() || detail::looksLikeOptionStart(tokens[i])) {
                throw CliError("option '" + std::string(tokForError) + "' requires a value",
                                ErrorKind::MissingValue);
            }
            collected[idx] = {slot.type.parse(tokens[i], slot.name)};
            provided[idx] = true;
            ++i;
        }
    }
};

}  // namespace cliforge
