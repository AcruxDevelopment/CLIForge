#pragma once

// Engine owns every registered Command and is the single entry point:
//
//   Engine cli;
//   cli.describe("A demo CLI");
//   cli.command()....action(&fn);
//   return cli.run(argc, argv);

#include <algorithm>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "Command.hpp"
#include "Levenshtein.hpp"

namespace cliforge
{

	class Engine
	{
	public:
		explicit Engine(std::string programName = "");

		Engine& describe(std::string description);

		// Registers a new, empty command and returns a reference to it so it
		// can be built up with the fluent .keyword()/.parameter()/... API.
		// The Engine owns it for the rest of the program's lifetime.
		Command& command();

		int run(int argc, char** argv);

	private:
		std::string programName_;
		std::string description_;
		std::vector<std::unique_ptr<Command>> commands_;

		static std::string basename(const std::string& path);
		static std::string join(const std::vector<std::string>& tokens);

		// Finds every command consistent with `queryTokens` as a prefix and
		// shows its help -- one command's help if there's exactly one match,
		// all of them (clearly separated) if the prefix is genuinely
		// ambiguous, or the global command list plus a hint if there's none.
		int showHelp(const std::vector<std::string>& queryTokens) const;

		void printGlobalHelp(std::ostream& os) const;
		void suggestCommands(const std::vector<std::string>& tokens, std::ostream& os) const;
		static std::string firstLine(const std::string& s);
		static std::string padRight(const std::string& s, std::size_t width);
	};
}
