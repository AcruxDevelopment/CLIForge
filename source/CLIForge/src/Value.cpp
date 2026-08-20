#pragma once
#include "CLIForge/Value.hpp"

namespace cliforge
{
	CliError::CliError(const std::string& msg, ErrorKind kind)
		: std::runtime_error(msg), m_kind(kind)
	{
	}

	ErrorKind CliError::kind() const noexcept
	{
		return kind();
	}

	ParseError::ParseError(const std::string& msg) : CliError(msg, ErrorKind::TypeMismatch) {}

	RegistrationError::RegistrationError(const std::string& msg) : CliError(msg, ErrorKind::Generic)
	{
	}

	Scalar ChoiceTable::parse(std::string_view token, std::string_view label) const
	{
		for (const auto& [name, val] : entries)
		{
			if (name == token)
				return val;
		}
		std::string allowed;
		for (size_t i = 0; i < entries.size(); ++i)
		{
			if (i)
				allowed += ", ";
			allowed += entries[i].first;
		}
		throw ParseError("type mismatch for '" + std::string(label) + "': got '" +
						 std::string(token) + "', expected one of: " + allowed);
	}

	bool TypeInfo::valid() const
	{
		return parsePlain != nullptr || choiceTable != nullptr;
	}

	Scalar TypeInfo::parse(std::string_view token, std::string_view label) const
	{
		return choiceTable ? choiceTable->parse(token, label) : parsePlain(token, label);
	}

	namespace detail
	{
		std::string toLower(std::string_view s)
		{
			std::string out(s);
			std::transform(out.begin(), out.end(), out.begin(),
						   [](unsigned char c) { return std::tolower(c); });
			return out;
		}
	}

	Value::Value() = default;

	Value Value::ofScalar(Scalar s)
	{
		Value v;
		v.m_data = std::move(s);
		return v;
	}
	Value Value::ofVector(std::vector<Scalar> v)
	{
		Value out;
		out.m_data = std::move(v);
		return out;
	}

	bool Value::hasValue() const
	{
		return !std::holds_alternative<std::monostate>(m_data);
	}
	bool Value::isVector() const
	{
		return std::holds_alternative<std::vector<Scalar>>(m_data);
	}
}
