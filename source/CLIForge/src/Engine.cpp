#pragma once
#include "CLIForge/Engine.hpp"

namespace cliforge
{
	Engine::Engine(std::string programName) : programName_(std::move(programName)) {}

	Engine& Engine::describe(std::string description)
	{
		description_ = std::move(description);
		return *this;
	}

	// Registers a new, empty command and returns a reference to it so it
	// can be built up with the fluent .keyword()/.parameter()/... API.
	// The Engine owns it for the rest of the program's lifetime.
	Command& Engine::command()
	{
		commands_.push_back(std::make_unique<Command>());
		return *commands_.back();
	}

	int Engine::run(int argc, char** argv)
	{
		if (programName_.empty() && argc > 0)
			programName_ = basename(argv[0]);
		for (auto& cmd : commands_)
		{
			if (!cmd->sealed())
			{
				std::cerr << "cliforge BUG: a command was registered without calling .action() "
							 "on it -- every command must be bound to a function.\n";
				return 2;
			}
		}

		std::vector<std::string> tokens;
		for (int i = 1; i < argc; ++i)
			tokens.emplace_back(argv[i]);

		if (tokens.empty())
		{
			printGlobalHelp(std::cout);
			return 0;
		}

		// Centralized help handling: `help [words...]`, or --help/-h
		// appearing anywhere. Both funnel into the same prefix search, so
		// an ambiguous prefix (e.g. "project --help" matching create/
		// delete/info) shows help for every command it's consistent with,
		// instead of silently picking one or matching nothing.
		std::optional<std::vector<std::string>> helpQuery;
		if (tokens[0] == "help")
		{
			helpQuery = std::vector<std::string>(tokens.begin() + 1, tokens.end());
		}
		else if (auto it = std::find_if(tokens.begin(), tokens.end(), [](const std::string& t)
										{ return t == "--help" || t == "-h"; });
				 it != tokens.end())
		{
			std::vector<std::string> q = tokens;
			q.erase(q.begin() + (it - tokens.begin()));
			helpQuery = std::move(q);
		}
		if (helpQuery)
		{
			return showHelp(*helpQuery);
		}

		struct Candidate
		{
			Command* cmd;
			StructuredMatch match;
		};
		std::vector<Candidate> candidates;
		std::string bestPartialError;
		std::vector<Command*> bestPartialCmds; // every command tied for the best partial score
		int bestPartialScore = -1;
		for (auto& cmd : commands_)
		{
			StructuredMatch m = cmd->tryMatchStructured(tokens);
			if (m.success)
			{
				candidates.push_back({cmd.get(), std::move(m)});
			}
			else if (!m.error.empty())
			{
				if (m.keywordScore > bestPartialScore)
				{
					bestPartialScore = m.keywordScore;
					bestPartialError = m.error;
					bestPartialCmds.clear();
					bestPartialCmds.push_back(cmd.get());
				}
				else if (m.keywordScore == bestPartialScore)
				{
					// Genuinely tied -- e.g. "project" alone is an equally
					// incomplete prefix of create/delete/info, all of which
					// report the identical "missing <name>" error. Rather
					// than silently picking whichever was registered first
					// (which read as an arbitrary, misleading suggestion),
					// remember all of them and show every option.
					bestPartialCmds.push_back(cmd.get());
				}
			}
		}

		if (candidates.empty())
		{
			if (!bestPartialCmds.empty())
			{
				// Every keyword matched some command, just with a bad or
				// missing argument value -- this is much more actionable
				// than a generic "no command matches" + fuzzy suggestions.
				std::cerr << "Error: " << bestPartialError << "\n";
				if (bestPartialCmds.size() == 1)
				{
					std::cerr << "Usage: " << programName_ << " "
							  << bestPartialCmds.front()->usageLine() << "\n";
				}
				else
				{
					std::cerr << "'" << join(tokens) << "' matches " << bestPartialCmds.size()
							  << " commands:\n";
					for (auto* cmd : bestPartialCmds)
					{
						std::string line = programName_ + " " + cmd->usageLine();
						std::cerr << "  " << padRight(line, 46) << firstLine(cmd->description())
								  << "\n";
					}
				}
				return 1;
			}
			std::cerr << "Error: no command matches '" << join(tokens) << "'.\n";
			suggestCommands(tokens, std::cerr);
			return 1;
		}

		std::sort(candidates.begin(), candidates.end(),
				  [](const Candidate& a, const Candidate& b)
				  {
					  if (a.match.keywordScore != b.match.keywordScore)
						  return a.match.keywordScore > b.match.keywordScore;
					  return a.cmd->structuredSlotCount() > b.cmd->structuredSlotCount();
				  });

		std::string firstError;
		Command* firstErrorCmd = nullptr;
		bool haveError = false;
		for (auto& c : candidates)
		{
			std::vector<std::string> remaining(tokens.begin() + static_cast<long>(c.match.consumed),
											   tokens.end());
			try
			{
				std::vector<Value> values = c.cmd->parseUnstructured(remaining);
				std::vector<Value> all = c.match.paramValues;
				for (auto& v : values)
					all.push_back(std::move(v));
				c.cmd->invoke(std::move(all));
				return 0;
			}
			catch (const CliError& e)
			{
				// Candidates are ranked best-first (most keyword matches
				// first), so the *first* failure is the one most likely to
				// be what the user actually meant -- keep that, not
				// whichever candidate happens to fail last.
				if (!haveError)
				{
					firstError = e.what();
					firstErrorCmd = c.cmd;
					haveError = true;
				}
			}
		}

		std::cerr << "Error: " << firstError << "\n";
		if (firstErrorCmd)
			std::cerr << "Usage: " << programName_ << " " << firstErrorCmd->usageLine() << "\n";
		return 1;
	}

	std::string Engine::basename(const std::string& path)
	{
		auto pos = path.find_last_of("/\\");
		return pos == std::string::npos ? path : path.substr(pos + 1);
	}

	std::string Engine::join(const std::vector<std::string>& tokens)
	{
		std::string out;
		for (std::size_t i = 0; i < tokens.size(); ++i)
		{
			if (i)
				out += ' ';
			out += tokens[i];
		}
		return out;
	}

	// Finds every command consistent with `queryTokens` as a prefix and
	// shows its help -- one command's help if there's exactly one match,
	// all of them (clearly separated) if the prefix is genuinely
	// ambiguous, or the global command list plus a hint if there's none.
	int Engine::showHelp(const std::vector<std::string>& queryTokens) const
	{
		if (queryTokens.empty())
		{
			printGlobalHelp(std::cout);
			return 0;
		}
		std::vector<Command*> matches;
		for (auto& cmd : commands_)
		{
			if (cmd->couldMatchPrefix(queryTokens))
				matches.push_back(cmd.get());
		}
		if (matches.empty())
		{
			std::cout << "No command matches '" << join(queryTokens) << "'.\n\n";
			printGlobalHelp(std::cout);
			return 1;
		}
		if (matches.size() == 1)
		{
			std::cout << matches.front()->helpText(programName_);
			return 0;
		}
		std::cout << "'" << join(queryTokens) << "' matches " << matches.size() << " commands:\n\n";
		for (std::size_t i = 0; i < matches.size(); ++i)
		{
			if (i)
				std::cout << "\n";
			std::cout << matches[i]->helpText(programName_);
		}
		return 0;
	}

	void Engine::printGlobalHelp(std::ostream& os) const
	{
		os << "Usage: " << programName_ << " <command> [ARGUMENTS] [OPTIONS]\n";
		if (!description_.empty())
			os << "\n" << description_ << "\n";
		os << "\nCommands:\n";
		for (auto& cmd : commands_)
		{
			std::string line = programName_ + " " + cmd->usageLine();
			os << "  " << padRight(line, 46) << firstLine(cmd->description()) << "\n";
		}
		os << "\nRun '" << programName_ << " help <command>' or add --help/-h to any command "
		   << "for details.\n";
	}

	void Engine::suggestCommands(const std::vector<std::string>& tokens, std::ostream& os) const
	{
		std::vector<std::pair<double, Command*>> scored;
		for (auto& cmd : commands_)
		{
			Command::FuzzyScore s = cmd->fuzzyStructuralDistance(tokens);
			double ratio = s.keywordChars ? static_cast<double>(s.distance) /
												static_cast<double>(s.keywordChars)
										  : 1.0;
			scored.emplace_back(ratio, cmd.get());
		}
		std::sort(scored.begin(), scored.end(), [](auto& a, auto& b) { return a.first < b.first; });

		std::vector<Command*> close;
		for (auto& [ratio, cmd] : scored)
		{
			if (close.size() >= 3)
				break;
			if (ratio <= 0.5)
				close.push_back(cmd);
		}
		if (close.empty())
			return;
		os << "\nDid you mean:\n";
		for (auto* cmd : close)
			os << "  " << programName_ << " " << cmd->usageLine() << "\n";
	}

	std::string Engine::firstLine(const std::string& s)
	{
		auto pos = s.find('\n');
		return pos == std::string::npos ? s : s.substr(0, pos);
	}

	std::string Engine::padRight(const std::string& s, std::size_t width)
	{
		if (s.size() >= width)
			return s + " ";
		return s + std::string(width - s.size(), ' ');
	}
}
