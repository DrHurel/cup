module;
#include <expected>
#include <functional>
#include <span>
#include <string>
export module cup.cmd:dispatch;

export import cup.error;

export namespace cup::cmd {

// Command is one cup subcommand: its name, one-line summary, and entry
// point. Mirrors Go's cmd.Command.
struct Command {
    std::string name;
    std::string summary;
    std::function<std::expected<void, error::Error>(std::span<const std::string>)> run;
};

// commands is the canonical list of cup subcommands, shared by run_main
// (dispatch + usage). Declared here, built in Dispatch.cpp.
[[nodiscard]] std::span<const Command> commands();

// run_main is cup's whole CLI entry point: help/no-args prints usage, an
// unknown command prints usage after an error, and a dispatched command's
// error is reported as "aborted." (ui.ErrAbort's equivalent) or "error: ...".
// Returns the process exit code. Kept here (not in main.cpp) so it is
// reachable from Catch2 like everything else in the port — main.cpp is a
// three-line shim over this.
[[nodiscard]] int run_main(std::span<const std::string> args);

}
