// A tiny, portable child process for process_test: what run_command and
// capture_command spawn instead of sh/true/false, which a Windows build has
// no equivalent of. Plain C++ with no cup imports, so it is the same program
// on every platform.
//
//   child_helper exit <n>        exit with status n
//   child_helper args [a...]     print each argument on its own line as <a>
//   child_helper cwd             print the working directory

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string_view>

int main(int argc, char** argv) {
    if (argc < 2) {
        return 64;
    }
    const std::string_view mode = argv[1];
    if (mode == "exit" && argc == 3) {
        return std::atoi(argv[2]);
    }
    if (mode == "args") {
        for (int i = 2; i < argc; ++i) {
            std::cout << '<' << argv[i] << ">\n";
        }
        return 0;
    }
    if (mode == "cwd") {
        std::cout << std::filesystem::current_path().string() << '\n';
        return 0;
    }
    return 64;
}
