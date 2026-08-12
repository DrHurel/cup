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

// commands is the canonical list of cup subcommands, shared by main
// (dispatch + usage). Declared here, built in Dispatch.cpp: only the 7
// commands ported so far (Phase 4 group 1) are listed; new/add, template/
// completion, compiler and docker/register are added as later groups of the
// migration land.
[[nodiscard]] std::span<const Command> commands();

}
