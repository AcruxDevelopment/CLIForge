// Exercises the *registration-time* safety guards: things a programmer
// could get wrong while wiring up commands, which should fail loudly and
// clearly at startup rather than misbehaving at run time -- plus a few
// "this should now succeed" checks for behavior that's supposed to work.
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

#include <cliforge/cliforge.hpp>

using cliforge::Command;
using cliforge::Engine;
using cliforge::RegistrationError;

enum class Color { Red, Green };

void noop(const std::string&) {}
void noop2(const std::string&, bool) {}
void noopOptStrStr(const std::optional<std::string>&, const std::string&) {}
void noopBoolOptVec(bool, const std::optional<std::string>&, const std::vector<std::string>&) {}
void noopWrongType(int32_t) {}
void noopColor(Color) {}

#define EXPECT_THROW(label, expr)                                 \
    do {                                                          \
        try {                                                     \
            expr;                                                 \
            std::printf("[FAIL] %-45s did not throw\n", label);   \
        } catch (const RegistrationError& e) {                    \
            std::printf("[ OK ] %-45s -> %s\n", label, e.what()); \
        }                                                          \
    } while (0)

#define EXPECT_OK(label, expr)                                              \
    do {                                                                    \
        try {                                                               \
            expr;                                                           \
            std::printf("[ OK ] %-45s (correctly did not throw)\n", label); \
        } catch (const RegistrationError& e) {                              \
            std::printf("[FAIL] %-45s threw: %s\n", label, e.what());       \
        }                                                                    \
    } while (0)

int main() {
    Engine cli;

    EXPECT_THROW("keyword after flag", ({
                     cli.command()
                         .keyword("a")
                         .flag("x", '\0', "")
                         .keyword("b");  // keywords must come before flags/options
                 }));

    EXPECT_THROW("keyword after variadic parameter", ({
                     cli.command()
                         .keyword("a")
                         .parameter<std::string>("p", "", /*variadic=*/true)
                         .keyword("b");  // variadic must be last in the structured part
                 }));

    EXPECT_THROW("loose parameter after variadic loose parameter", ({
                     cli.command()
                         .keyword("a")
                         .flag("f", '\0', "")
                         .parameter<std::string>("p1", "", /*variadic=*/true)
                         .parameter<std::string>("p2", "");  // variadic must be the last parameter
                 }));

    EXPECT_THROW("action() arity mismatch", ({
                     cli.command().keyword("a").parameter<std::string>("p", "").action(&noop2);
                     // noop2 takes 2 args, only 1 slot declared
                 }));

    EXPECT_THROW("no keyword at all", ({
                     cli.command().parameter<std::string>("p", "").action(&noop);
                     // a command must have at least one keyword
                 }));

    EXPECT_THROW("option bound to a required (bare) type", ({
                     cli.command().keyword("a").option<std::string>("o", '\0', "").action(&noop);
                     // options must be std::optional<T> or std::vector<T> -- never bare T
                 }));

    EXPECT_THROW("declared type doesn't match action() type", ({
                     cli.command()
                         .keyword("a")
                         .parameter<std::string>("p", "")
                         .action(&noopWrongType);  // declared string, action expects int32_t
                 }));

    EXPECT_THROW("enum parameter without choices<T>()", ({
                     cli.command()
                         .keyword("a")
                         .parameter<Color>("c", "")
                         .action(&noopColor);  // never called .choices<Color>(...)
                 }));

    EXPECT_THROW("duplicate long flag name", ({
                     cli.command()
                         .keyword("a")
                         .flag("force", 'f', "")
                         .flag("force", 'g', "");
                 }));

    EXPECT_THROW("duplicate short flag name", ({
                     cli.command()
                         .keyword("a")
                         .flag("force", 'f', "")
                         .flag("full", 'f', "");
                 }));

    EXPECT_THROW("reserved --help name", ({ cli.command().keyword("a").flag("help", '\0', ""); }));

    EXPECT_THROW("reserved -h short name", ({ cli.command().keyword("a").flag("x", 'h', ""); }));

    EXPECT_THROW("invalid keyword charset", ({ cli.command().keyword("bad keyword!"); }));

    EXPECT_THROW("keyword starting with dash", ({ cli.command().keyword("-bad"); }));

    EXPECT_OK("parameter() after option() (loose parameter)", ({
                  cli.command()
                      .keyword("a")
                      .option<std::string>("o", '\0', "")
                      .parameter<std::string>("p", "")
                      .action(&noopOptStrStr);
              }));

    EXPECT_OK("flag/option/loose-parameter fully interleaved", ({
                  cli.command()
                      .keyword("b")
                      .flag("f", '\0', "")
                      .option<std::string>("o", '\0', "")
                      .parameter<std::string>("p", "", /*variadic=*/true)
                      .action(&noopBoolOptVec);
              }));

    EXPECT_OK("action() taking arguments by const reference", ({
                  cli.command().keyword("c").parameter<std::string>("p", "").action(&noop);
              }));

    std::printf("\nAll registration-guard checks completed.\n");
    return 0;
}
