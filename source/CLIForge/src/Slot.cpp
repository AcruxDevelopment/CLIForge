#pragma once
#include "CLIForge/Slot.hpp"

namespace cliforge
{
	std::string Slot::longFlag() const
	{
		return "--" + name;
	}
	std::string Slot::shortFlag() const
	{
		return shortName ? std::string("-") + shortName : std::string();
	}
}
