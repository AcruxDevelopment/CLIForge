// packctl -- a demo CLI built with cliforge, showing every rule from the
// spec: keyword charset (digits/dots/underscores/dashes, digit-leading),
// explicitly typed positional + variadic parameters (both in the
// structured part AND as "loose" parameters in the unstructured part),
// combinable short flags, typed (possibly variadic, possibly enum)
// options that are always optional, structured vs unstructured command
// parts, custom enums, and free-function-only registration. Handlers
// take their arguments by value, by reference, or by const reference,
// whichever fits.
#include <cstdio>

#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include <CLIForge/CLIForge.hpp>

using cliforge::Engine;

// ---------------------------------------------------------------------
// Custom enums -- plain C++ enum class, nothing cliforge-specific here.
// ---------------------------------------------------------------------
enum class ProjectTemplate
{
	Cpp,
	Python,
	Node
};
enum class Editor
{
	Vim,
	Emacs,
	Nano
};
enum class TwoFactorMethod
{
	App,
	Sms,
	Email
};
enum class UpdateChannel
{
	Stable,
	Beta,
	Nightly
};

// ---------------------------------------------------------------------
// Every command handler is a completely ordinary free function. There is
// no cliforge type anywhere in these signatures. Options are always
// std::optional<T> (or std::vector<T> if variadic) because options are
// never required -- only parameters are. Arguments are taken by value,
// const reference, or reference, whichever avoids unnecessary copies.
// ---------------------------------------------------------------------

void projectCreate(const std::string& name, std::optional<ProjectTemplate> tmpl, bool git)
{
	static const char* s_names[] = {"cpp", "python", "node"};
	ProjectTemplate t = tmpl.value_or(ProjectTemplate::Cpp);
	std::printf("[create] project '%s' from template '%s' (git init: %s)\n", name.c_str(),
				s_names[static_cast<int>(t)], git ? "yes" : "no");
}

void projectDelete(const std::string& name, bool force, const std::optional<std::string>& reason)
{
	std::printf("[delete] project '%s' (force=%s, reason=%s)\n", name.c_str(), force ? "yes" : "no",
				reason ? reason->c_str() : "<none given>");
}

void projectInfo(const std::string& name, bool json)
{
	if (json)
	{
		std::printf("{\"project\": \"%s\", \"status\": \"ok\"}\n", name.c_str());
	}
	else
	{
		std::printf("[info] project '%s': status ok\n", name.c_str());
	}
}

void build(const std::vector<int32_t>& targets, bool noAssets, bool debug, bool verbose,
		   const std::vector<std::string>& architectures, std::optional<Editor> editor,
		   std::optional<int32_t> threshold)
{
	static const char* s_editorNames[] = {"vim", "emacs", "nano"};
	std::printf("[build] targets=[");
	for (size_t i = 0; i < targets.size(); ++i)
		std::printf("%s%d", i ? "," : "", targets[i]);
	std::printf("] noAssets=%d debug=%d verbose=%d architectures=[", noAssets, debug, verbose);
	for (size_t i = 0; i < architectures.size(); ++i)
		std::printf("%s%s", i ? "," : "", architectures[i].c_str());
	std::printf("] editor=%s threshold=%s\n",
				editor ? s_editorNames[static_cast<int>(*editor)] : "<none>",
				threshold ? std::to_string(*threshold).c_str() : "<default>");
}

void enable2fa(std::optional<TwoFactorMethod> method)
{
	static const char* s_names[] = {"app", "sms", "email"};
	TwoFactorMethod m = method.value_or(TwoFactorMethod::App);
	std::printf("[2fa] enabled via %s\n", s_names[static_cast<int>(m)]);
}

void disable2fa()
{
	std::printf("[2fa] disabled\n");
}

void selfUpdate(std::optional<UpdateChannel> channel, bool checkOnly)
{
	static const char* s_names[] = {"stable", "beta", "nightly"};
	std::printf("[self-update] channel=%s checkOnly=%d\n",
				channel ? s_names[static_cast<int>(*channel)] : "stable(default)", checkOnly);
}

void status(bool json)
{
	std::printf(json ? "{\"status\":\"ok\"}\n" : "All systems operational.\n");
}

// `deploy` showcases a *loose* parameter: `hosts` is declared after
// flag()/option(), so it lives in the unstructured part. The user can
// type it anywhere relative to --env/-n, e.g.
//   packctl deploy web1 --env prod web2 -n web3
// still collects hosts=[web1,web2,web3], matched by position among
// themselves, not by where they fall relative to the option/flag.
void deploy(bool dryRun, const std::optional<std::string>& env,
			const std::vector<std::string>& hosts)
{
	std::printf("[deploy] dryRun=%d env=%s hosts=[", dryRun, env ? env->c_str() : "<none>");
	for (size_t i = 0; i < hosts.size(); ++i)
		std::printf("%s%s", i ? "," : "", hosts[i].c_str());
	std::printf("]\n");
}

// ---------------------------------------------------------------------
// Registration: this is the entirety of the "wiring" -- names,
// descriptions, structure, and types. action() then just validates that
// the bound function's arguments are consistent with what was declared.
// ---------------------------------------------------------------------

int main(int argc, char** argv)
{
	Engine cli("packctl");
	cli.describe("A demo package/project CLI built with cliforge.");

	cli.command()
		.keyword("project")
		.parameter<std::string>("name", "Project name")
		.keyword("create")
		.option<ProjectTemplate>("template", 't', "Project template to scaffold (default: cpp)")
		.choices<ProjectTemplate>({{"cpp", ProjectTemplate::Cpp},
								   {"python", ProjectTemplate::Python},
								   {"node", ProjectTemplate::Node}})
		.flag("git", 'g', "Initialize a git repository")
		.describe("Scaffold a new project from a template")
		.action(&projectCreate);

	cli.command()
		.keyword("project")
		.parameter<std::string>("name", "Project name")
		.keyword("delete")
		.flag("force", 'f', "Skip the confirmation prompt")
		.option<std::string>("reason", 'r', "Why the project is being deleted")
		.describe("Permanently delete a project by name")
		.action(&projectDelete);

	cli.command()
		.keyword("project")
		.parameter<std::string>("name", "Project name")
		.flag("json", 'j', "Print machine-readable JSON")
		.describe("Show information about a project")
		.action(&projectInfo);

	cli.command()
		.keyword("build")
		.parameter<int32_t>("targets", "Target ids to build", /*variadic=*/true)
		.flag("no-assets", 'n', "Skip packaging assets")
		.flag("debug", 'd', "Build with debug symbols")
		.flag("verbose", 'i', "Verbose build output")
		.option<std::string>("architectures", 'a', "Target architectures", /*variadic=*/true)
		.option<Editor>("editor", 'e', "Editor to open on completion")
		.choices<Editor>({{"vim", Editor::Vim}, {"emacs", Editor::Emacs}, {"nano", Editor::Nano}})
		.option<int32_t>("threshold", 't', "Warning threshold (0 = strict)")
		.describe("Build the given target ids")
		.action(&build);

	cli.command()
		.keyword("2fa")
		.keyword("enable")
		.option<TwoFactorMethod>("method", 'm', "Verification method (default: app)")
		.choices<TwoFactorMethod>({{"app", TwoFactorMethod::App},
								   {"sms", TwoFactorMethod::Sms},
								   {"email", TwoFactorMethod::Email}})
		.describe("Turn on two-factor authentication")
		.action(&enable2fa);

	cli.command()
		.keyword("2fa")
		.keyword("disable")
		.describe("Turn off two-factor authentication")
		.action(&disable2fa);

	cli.command()
		.keyword("self-update")
		.option<UpdateChannel>("channel", 'c', "Release channel to update from")
		.choices<UpdateChannel>({{"stable", UpdateChannel::Stable},
								 {"beta", UpdateChannel::Beta},
								 {"nightly", UpdateChannel::Nightly}})
		.flag("check-only", 'k', "Check for updates without installing")
		.describe("Update packctl to the latest version")
		.action(&selfUpdate);

	cli.command()
		.keyword("status")
		.flag("json", 'j', "Print machine-readable JSON")
		.describe("Show whether everything is operational")
		.action(&status);

	cli.command()
		.keyword("deploy")
		.flag("dry-run", 'n', "Show what would happen without doing it")
		.option<std::string>("env", 'e', "Environment name")
		.parameter<std::string>("hosts", "Target hostnames", /*variadic=*/true) // loose!
		.describe("Deploy to the given hosts, in any order relative to --env/-n")
		.action(&deploy);

	return cli.run(argc, argv);
}
