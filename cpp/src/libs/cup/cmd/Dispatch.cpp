module;
#include <array>
#include <functional>
#include <span>
#include <typeinfo>
module cup.cmd;

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

}
