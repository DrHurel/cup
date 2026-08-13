#include <string>
#include <vector>

import cup.cmd;

int main(int argc, char** argv) {
    const std::vector<std::string> args(argv + (argc > 0 ? 1 : 0), argv + argc);
    return cup::cmd::run_main(args);
}
