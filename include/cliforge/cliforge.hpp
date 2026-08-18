#pragma once
//
// cliforge.hpp
//
// cliforge: a small header-only C++20 library for building CLIs whose
// commands are just free functions.
//
//   #include <cliforge/cliforge.hpp>
//
//   void greet(std::string name, bool loud) {
//       std::cout << (loud ? "HELLO, " : "Hello, ") << name << (loud ? "!!" : ".") << "\n";
//   }
//
//   int main(int argc, char** argv) {
//       cliforge::Engine cli("greeter");
//       cli.command()
//           .keyword("greet", "Say hello to someone")
//           .parameter("name", "Who to greet")
//           .flag("loud", "l", "SHOUT the greeting")
//           .describe("Greets someone by name")
//           .action(&greet);
//       return cli.run(argc, argv);
//   }
//
// See README.md for the full guide.
//
#include "command.hpp"
#include "engine.hpp"
#include "function_traits.hpp"
#include "levenshtein.hpp"
#include "slot.hpp"
#include "value.hpp"
