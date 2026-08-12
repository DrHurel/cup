#include <cstdio>
#include <span>
#include <string>
#include <vector>

import cup.cmd;
import cup.ui;

namespace {

void usage() {
    std::puts("cup — scaffold and manage C++23-modules projects");
    std::puts("");
    std::puts("usage: cup <command> [args]");
    std::puts("");
    std::puts("commands:");
    for (const auto& c : cup::cmd::commands()) {
        std::printf("  %-11s %s\n", c.name.c_str(), c.summary.c_str());
    }
    std::puts("");
    std::puts(
        "MODE is one of Debug (default), Release, or Coverage; each gets its own "
        "build/<MODE> tree.");
}

}  // namespace

int main(int argc, char** argv) {
    const std::vector<std::string> args(argv + (argc > 0 ? 1 : 0), argv + argc);

    if (args.empty() || args[0] == "-h" || args[0] == "--help" || args[0] == "help") {
        usage();
        return 0;
    }

    const std::string& name = args[0];
    const std::span<const std::string> rest(args.begin() + 1, args.end());

    for (const auto& c : cup::cmd::commands()) {
        if (c.name == name) {
            if (auto result = c.run(rest); !result.has_value()) {
                const auto& err = result.error();
                if (cup::error::is_abort(err)) {
                    cup::ui::err("aborted.");
                } else {
                    cup::ui::err("error: " + err.message());
                }
                return 1;
            }
            return 0;
        }
    }

    cup::ui::err("unknown command \"" + name + "\"");
    usage();
    return 1;
}
