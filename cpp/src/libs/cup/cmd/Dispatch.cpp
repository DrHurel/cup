module;
#include <algorithm>
#include <array>
#include <expected>
#include <functional>
#include <print>
#include <span>
#include <string>
#include <typeinfo>
module cup.cmd;

import cup.ui;

namespace cup::cmd {

std::span<const Command> commands() {
    static const std::array<Command, 11> table{{
        {"new", "create a new C++ project", &run_new},
        {"add", "scaffold an app, lib, or test (interactive)", &run_add},
        {"configure", "generate the CMake build system [MODE]", &run_configure},
        {"build", "configure + compile [MODE]", &run_build},
        {"rebuild", "wipe build/ then compile [MODE]", &run_rebuild},
        {"run", "build then run an app [MODE] [app] [-- args]", &run_run},
        {"test", "build then run the test suite [MODE]", &run_test},
        {"retest", "wipe build/ then run the test suite [MODE]", &run_retest},
        {"clean", "remove the build/ directory", &run_clean},
        {"template", "list or add project-local templates <list|new>", &run_template},
        {"completion", "install or print shell completion <install|bash|zsh|fish>", &run_completion},
    }};
    return table;
}

namespace {

void usage() {
    std::println("cup — scaffold and manage C++23-modules projects");
    std::println();
    std::println("usage: cup <command> [args]");
    std::println();
    std::println("commands:");
    for (const auto& c : commands()) {
        std::println("  {:<11} {}", c.name, c.summary);
    }
    std::println();
    std::println(
        "MODE is one of Debug (default), Release, or Coverage; each gets its own "
        "build/<MODE> tree.");
}

}  // namespace

int run_main(std::span<const std::string> args) {
    if (args.empty() || args[0] == "-h" || args[0] == "--help" || args[0] == "help") {
        usage();
        return 0;
    }

    const std::string& name = args[0];
    const std::span<const std::string> rest(args.begin() + 1, args.end());

    const auto cmds = commands();
    const auto it = std::ranges::find(cmds, name, &Command::name);
    if (it == cmds.end()) {
        ui::err("unknown command \"" + name + "\"");
        usage();
        return 1;
    }

    if (auto result = it->run(rest); !result.has_value()) {
        if (const auto& err = result.error(); error::is_abort(err)) {
            ui::err("aborted.");
        } else {
            ui::err("error: " + err.message());
        }
        return 1;
    }
    return 0;
}

}
