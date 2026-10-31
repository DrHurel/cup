module;
#include <algorithm>
#include <array>
#include <chrono>
#include <expected>
#include <format>
#include <functional>
#include <print>
#include <span>
#include <string>
#include <string_view>
#include <typeinfo>
#include <cup/version.h>
module cmd;

import library;
import logging;
import core;
import ui;

namespace cmd {

std::span<const Command> commands() {
    static const std::array<Command, 16> table{{
        {"new", "create a new C++ project", &run_new},
        {"init", "turn the current directory's project into a cup project", &run_init},
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
        {"compiler", "show or change minimum compiler versions <set|verify>", &run_compiler},
        {"docker", "manage build images <new|build|push>", &run_docker},
        {"register", "register a third-party dependency", &run_register},
        {"unregister", "remove a third-party dependency [name]", &run_unregister},
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

// print_version reports cup's own cup_version (from this project's cup.toml,
// baked in at build time) plus the commit it was built from — absent only for
// a build made outside a git checkout.
void print_version() {
    constexpr std::string_view sha = CUP_BUILD_SHA;
    if (sha.empty()) {
        std::println("cup {}", CUP_VERSION);
    } else {
        std::println("cup {} ({})", CUP_VERSION, sha);
    }
}

}  // namespace

int run_main(std::span<const std::string> args) {
    // Initializes every library cmd depends on, dependencies first
    // (core's initialize() sets up the console: UTF-8 output and ANSI colours
    // on Windows, a no-op elsewhere), and finalizes them in reverse when the
    // command returns.
    const library::Scope<Library> libraries;

    // A failed init() (e.g. an unwritable log dir) must never stop cup from
    // running the actual command — just tell the user how to quiet it.
    if (auto logged = logging::init(); !logged.has_value()) {
        ui::err(std::format("warning: {}", logged.error().message()));
    }

    if (args.empty() || args[0] == "-h" || args[0] == "--help" || args[0] == "help") {
        usage();
        return 0;
    }
    if (args[0] == "-v" || args[0] == "--version") {
        print_version();
        return 0;
    }

    const std::string& name = args[0];
    const std::span<const std::string> rest(args.begin() + 1, args.end());

    const auto cmds = commands();
    const auto it = std::ranges::find(cmds, name, &Command::name);
    if (it == cmds.end()) {
        logging::user::warn(std::format("command={} status=unknown", name));
        ui::err("unknown command \"" + name + "\"");
        usage();
        return 1;
    }

    const auto start = std::chrono::steady_clock::now();
    auto result = it->run(rest);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - start)
                        .count();
    logging::user::info(std::format("command={} status={} duration_ms={}", name,
                                result.has_value() ? "ok" : "error", ms));

    if (!result.has_value()) {
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
