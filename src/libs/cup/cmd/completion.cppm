module;
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>
export module cup.cmd:completion;

export import cup.error;

// Declarations only, defined in Completion.cpp: the definitions use
// <filesystem>, <fstream> and <cstdlib> (getenv) throughout, none of which
// this interface partition needs to expose.
export namespace cup::cmd {

// run_completion prints a shell completion script for the requested shell to
// stdout, or, given "install", wires it into the shell's startup so
// completion works automatically with no manual sourcing.
[[nodiscard]] std::expected<void, error::Error> run_completion(std::span<const std::string> args);

// subcommand_names returns every cup subcommand name, from commands() (the
// single source of truth) so completion never drifts from dispatch.
[[nodiscard]] std::vector<std::string> subcommand_names();

// script_for returns the completion script for a supported shell.
[[nodiscard]] std::expected<std::string, error::Error> script_for(std::string_view shell);

// detect_shell guesses the current shell from $SHELL, defaulting to bash.
[[nodiscard]] std::string detect_shell();

[[nodiscard]] std::string bash_completion();
[[nodiscard]] std::string zsh_completion();
[[nodiscard]] std::string fish_completion();

}
