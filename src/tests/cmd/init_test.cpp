#include <catch2/catch_test_macros.hpp>

#include <expected>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "TempDir.hpp"
#include "TestCompat.hpp"

import cmd;
import project;
import scaffold;
import ui;

namespace {

using cup::test::TempDir;

// Points the real stdin fd at a pipe holding keys, so ui's is_tty(kStdinFd)
// check sees "not a terminal" and prompts read from ScopedInput instead.
// Mirrors new_add_test.cpp's ScopedStdin.
class ScopedStdin {
public:
    explicit ScopedStdin(std::string_view keys) {
        int fds[2]{};
        REQUIRE(::pipe(fds) == 0);
        saved_ = ::dup(STDIN_FILENO);
        REQUIRE(saved_ >= 0);
        REQUIRE(::write(fds[1], keys.data(), static_cast<unsigned>(keys.size())) ==
                static_cast<int>(keys.size()));
        REQUIRE(::close(fds[1]) == 0);
        REQUIRE(::dup2(fds[0], STDIN_FILENO) == STDIN_FILENO);
        REQUIRE(::close(fds[0]) == 0);
    }

    ScopedStdin(const ScopedStdin&) = delete;
    ScopedStdin& operator=(const ScopedStdin&) = delete;

    ~ScopedStdin() {
        ::dup2(saved_, STDIN_FILENO);
        ::close(saved_);
    }

private:
    int saved_ = -1;
};

// Changes the working directory for the test's lifetime: run_init adopts
// whatever directory it runs in.
class ScopedCwd {
public:
    explicit ScopedCwd(const std::filesystem::path& dir)
        : previous_(std::filesystem::current_path()) {
        std::filesystem::current_path(dir);
    }
    ~ScopedCwd() { std::filesystem::current_path(previous_); }
    ScopedCwd(const ScopedCwd&) = delete;
    ScopedCwd& operator=(const ScopedCwd&) = delete;

private:
    std::filesystem::path previous_;
};

std::string read(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

std::size_t count(std::string_view haystack, std::string_view needle) {
    std::size_t n = 0;
    for (auto pos = haystack.find(needle); pos != std::string_view::npos;
         pos = haystack.find(needle, pos + 1)) {
        ++n;
    }
    return n;
}

// run_init_in runs `cup init args...` from dir with answers fed to any
// prompt, returning its result.
std::expected<void, error::Error> run_init_in(const TempDir& dir,
                                                   const std::vector<std::string>& args,
                                                   std::string_view answers = "") {
    const ScopedCwd cwd(dir.path());
    const ScopedStdin not_a_terminal("");
    std::istringstream in{std::string(answers)};
    const ui::ScopedInput scoped(in);
    return cmd::run_init(args);
}

constexpr std::string_view kExistingCMake = R"(cmake_minimum_required(VERSION 3.20)
project(legacy_app
        VERSION 1.2
        LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
add_executable(legacy main.cpp)
)";

}  // namespace

// --- detection helpers ------------------------------------------------------

TEST_CASE("parse_std_token maps build-file spellings onto supported standards", "[init]") {
    REQUIRE(cmd::parse_std_token("11") == 11);
    REQUIRE(cmd::parse_std_token("0x") == 11);
    REQUIRE(cmd::parse_std_token("1y") == 14);
    REQUIRE(cmd::parse_std_token("1Z") == 17);
    REQUIRE(cmd::parse_std_token("2a") == 20);
    REQUIRE(cmd::parse_std_token("2b") == 23);
    REQUIRE(cmd::parse_std_token("23") == 23);
    REQUIRE_FALSE(cmd::parse_std_token("98").has_value());
    REQUIRE_FALSE(cmd::parse_std_token("26").has_value());
    REQUIRE_FALSE(cmd::parse_std_token("").has_value());
}

TEST_CASE("detect_cmake_standard finds the newest standard a CMakeLists asks for", "[init]") {
    REQUIRE(cmd::detect_cmake_standard("set(CMAKE_CXX_STANDARD 17)") == 17);
    REQUIRE(cmd::detect_cmake_standard(R"(set(CMAKE_CXX_STANDARD "14"))") == 14);
    REQUIRE(cmd::detect_cmake_standard(
                "set_target_properties(app PROPERTIES CXX_STANDARD 20)") == 20);
    REQUIRE(cmd::detect_cmake_standard(
                "target_compile_features(app PUBLIC cxx_std_23)") == 23);
    REQUIRE(cmd::detect_cmake_standard(R"(add_compile_options(-std=gnu++2a))") == 20);
    REQUIRE(cmd::detect_cmake_standard(
                "set(CMAKE_CXX_STANDARD 11)\ntarget_compile_features(x PUBLIC cxx_std_17)") == 17);
    // A commented-out setting doesn't count, and nothing at all is 0.
    REQUIRE(cmd::detect_cmake_standard("# set(CMAKE_CXX_STANDARD 20)\n") == 0);
    REQUIRE(cmd::detect_cmake_standard("project(x)") == 0);
}

TEST_CASE("detect_make_standard reads -std= flags, MSVC's /std: included", "[init]") {
    REQUIRE(cmd::detect_make_standard("CXXFLAGS := -O2 -std=c++14 -Wall") == 14);
    REQUIRE(cmd::detect_make_standard("CXXFLAGS += /std:c++17") == 17);
    REQUIRE(cmd::detect_make_standard("# CXXFLAGS = -std=c++20\nCXXFLAGS = -O2") == 0);
}

TEST_CASE("detect_cmake_name reads the first project() call", "[init]") {
    REQUIRE(cmd::detect_cmake_name(kExistingCMake) == "legacy_app");
    REQUIRE(cmd::detect_cmake_name("PROJECT( Shouty CXX)") == "Shouty");
    REQUIRE(cmd::detect_cmake_name("project(${NAME})").empty());
    REQUIRE(cmd::detect_cmake_name("add_executable(x main.cpp)").empty());
}

TEST_CASE("sanitize_ident turns a directory name into an identifier", "[init]") {
    REQUIRE(cmd::sanitize_ident("my-project.v2") == "my_project_v2");
    REQUIRE(cmd::sanitize_ident("2048game") == "_2048game");
    REQUIRE(cmd::sanitize_ident("ok_name") == "ok_name");
    REQUIRE(cmd::sanitize_ident("").empty());
}

TEST_CASE("with_compiler_guard inserts the guard after a multi-line project()", "[init]") {
    const auto out = cmd::with_compiler_guard(kExistingCMake);
    REQUIRE(out.has_value());
    const auto guard = out->find(scaffold::kGuardStart);
    REQUIRE(guard != std::string::npos);
    REQUIRE(out->find("LANGUAGES CXX)") < guard);
    REQUIRE(guard < out->find("set(CMAKE_CXX_STANDARD 17)"));
    REQUIRE(out->find(scaffold::kGuardEnd) != std::string::npos);
    // Everything else is kept verbatim.
    REQUIRE(out->find("add_executable(legacy main.cpp)") != std::string::npos);

    REQUIRE_FALSE(cmd::with_compiler_guard("add_executable(x main.cpp)\n").has_value());
}

TEST_CASE("with_compiler_guard skips a ')' inside a quoted project() argument", "[init]") {
    const std::string text = "project(demo\n  DESCRIPTION \"a tool (fast) \\\"really\\\"\"\n"
                             "  LANGUAGES CXX)\nadd_executable(demo main.cpp)\n";
    const auto out = cmd::with_compiler_guard(text);
    REQUIRE(out.has_value());
    const auto guard = out->find(scaffold::kGuardStart);
    REQUIRE(out->find("LANGUAGES CXX)") < guard);
    REQUIRE(guard < out->find("add_executable(demo main.cpp)"));
}

TEST_CASE("detect_project tells CMake, Make and bare directories apart", "[init]") {
    SECTION("CMake: name from project(), standard from any CMakeLists in the tree") {
        const TempDir dir;
        dir.write("CMakeLists.txt", "project(demo CXX)\nadd_subdirectory(src)\n");
        dir.write("src/CMakeLists.txt", "target_compile_features(demo PUBLIC cxx_std_20)\n");
        // A vendored dependency's own requirements don't count.
        dir.write("third_party/big/CMakeLists.txt", "set(CMAKE_CXX_STANDARD 23)\n");
        dir.write("build/CMakeLists.txt", "set(CMAKE_CXX_STANDARD 23)\n");
        const auto d = cmd::detect_project(dir.path());
        REQUIRE(d.build_tool == "cmake");
        REQUIRE(d.name == "demo");
        REQUIRE(d.standard == 20);
    }
    SECTION("Make") {
        const TempDir dir;
        dir.write("Makefile", "CXXFLAGS = -std=c++11\n");
        const auto d = cmd::detect_project(dir.path());
        REQUIRE(d.build_tool == "make");
        REQUIRE(d.standard == 11);
        REQUIRE(d.name.empty());
    }
    SECTION("nothing to go on") {
        const TempDir dir;
        dir.write("main.cpp", "int main() {}\n");
        const auto d = cmd::detect_project(dir.path());
        REQUIRE(d.build_tool.empty());
        REQUIRE(d.standard == 0);
    }
}

// --- run_init ---------------------------------------------------------------

TEST_CASE("run_init adopts an existing CMake project in place", "[init][end-to-end]") {
    const TempDir dir;
    dir.write("CMakeLists.txt", kExistingCMake);
    dir.write("main.cpp", "int main() { return 0; }\n");
    dir.write(".gitignore", "*.swp\n/build/\n");

    REQUIRE(run_init_in(dir, {}).has_value());

    const auto proj = project::find_from(dir.path());
    REQUIRE(proj.has_value());
    REQUIRE(proj->config.name == "legacy_app");
    REQUIRE(proj->config.cpp_standard == 17);
    REQUIRE(proj->config.tool() == "cmake");
    // No floors or build image are claimed for a project cup never checked.
    REQUIRE_FALSE(proj->config.compiler.has_floor());
    REQUIRE(proj->config.docker.images.empty());

    const std::string cmake = read(dir.path() / "CMakeLists.txt");
    REQUIRE(cmake.find(scaffold::kGuardStart) != std::string::npos);
    REQUIRE(cmake.find("add_executable(legacy main.cpp)") != std::string::npos);
    REQUIRE(cmake.find("add_subdirectory(src/libs)") < cmake.find("add_subdirectory(src/apps)"));
    REQUIRE(std::filesystem::exists(dir.path() / "src" / "libs" / "CMakeLists.txt"));
    REQUIRE(std::filesystem::exists(dir.path() / "src" / "apps" / "CMakeLists.txt"));

    // Existing sources are untouched; the .gitignore is merged, not replaced.
    REQUIRE(read(dir.path() / "main.cpp") == "int main() { return 0; }\n");
    const std::string gitignore = read(dir.path() / ".gitignore");
    REQUIRE(gitignore.starts_with("*.swp\n/build/\n"));
    REQUIRE(count(gitignore, "/build/") == 1);
    REQUIRE(gitignore.find("compile_commands.json") != std::string::npos);

    // The guard is the real thing: `cup compiler set` can now rewrite it.
    REQUIRE(scaffold::replace_compiler_guard(dir.path(), dir.path() / "CMakeLists.txt", 13, 0)
                .has_value());
    REQUIRE(read(dir.path() / "CMakeLists.txt").find("VERSION_LESS 13") != std::string::npos);
}

TEST_CASE("run_init leaves an existing src/apps or src/libs layout alone", "[init]") {
    const TempDir dir;
    dir.write("CMakeLists.txt", "project(demo CXX)\nset(CMAKE_CXX_STANDARD 20)\n"
                                "add_subdirectory(src/apps)\n");
    dir.write("src/apps/CMakeLists.txt", "add_subdirectory(tool)\n");

    REQUIRE(run_init_in(dir, {}).has_value());

    const std::string cmake = read(dir.path() / "CMakeLists.txt");
    // Wired exactly once: a second add_subdirectory(src/apps) is a CMake error.
    REQUIRE(count(cmake, "add_subdirectory(src/apps)") == 1);
    REQUIRE(count(cmake, "add_subdirectory(src/libs)") == 1);
    REQUIRE(read(dir.path() / "src" / "apps" / "CMakeLists.txt") == "add_subdirectory(tool)\n");
}

TEST_CASE("run_init adopts a Makefile project without touching the Makefile", "[init]") {
    const TempDir dir;
    const std::string makefile = "CXXFLAGS = -std=c++14\nall:\n\t$(CXX) $(CXXFLAGS) main.cpp\n";
    dir.write("Makefile", makefile);

    REQUIRE(run_init_in(dir, {"mk_demo"}).has_value());

    const auto proj = project::find_from(dir.path());
    REQUIRE(proj.has_value());
    REQUIRE(proj->config.name == "mk_demo");
    REQUIRE(proj->uses_make());
    REQUIRE(proj->config.cpp_standard == 14);
    REQUIRE(read(dir.path() / "Makefile") == makefile);
    REQUIRE(std::filesystem::exists(dir.path() / ".gitignore"));
}

TEST_CASE("run_init scaffolds a fresh tree in a directory with no build file", "[init]") {
    const TempDir dir;
    dir.write("notes.txt", "keep me\n");

    // Prompts: build system (1 = cmake). The standard comes from --std.
    REQUIRE(run_init_in(dir, {"fresh", "--std", "20"}, "1\n").has_value());

    const auto proj = project::find_from(dir.path());
    REQUIRE(proj.has_value());
    REQUIRE(proj->config.cpp_standard == 20);
    REQUIRE(read(dir.path() / "CMakeLists.txt").find("project(fresh") != std::string::npos);
    REQUIRE(std::filesystem::exists(dir.path() / "src" / "apps" / "CMakeLists.txt"));
    REQUIRE(read(dir.path() / "notes.txt") == "keep me\n");
}

TEST_CASE("run_init flags override detection", "[init]") {
    const TempDir dir;
    dir.write("CMakeLists.txt", kExistingCMake);

    REQUIRE(run_init_in(dir, {"--std=23", "--build-tool=cmake", "renamed"}).has_value());

    const auto proj = project::find_from(dir.path());
    REQUIRE(proj.has_value());
    REQUIRE(proj->config.name == "renamed");
    REQUIRE(proj->config.cpp_standard == 23);
}

TEST_CASE("run_init falls back to the directory name, made an identifier", "[init]") {
    const TempDir parent;
    const std::filesystem::path root = parent.path() / "my-tool";
    std::filesystem::create_directories(root);
    {
        std::ofstream(root / "CMakeLists.txt") << "project(${PROJ})\nset(CMAKE_CXX_STANDARD 17)\n";
    }
    {
        const ScopedCwd cwd(root);
        const ScopedStdin not_a_terminal("");
        std::istringstream in;
        const ui::ScopedInput scoped(in);
        REQUIRE(cmd::run_init({}).has_value());
    }
    const auto proj = project::find_from(root);
    REQUIRE(proj.has_value());
    REQUIRE(proj->config.name == "my_tool");
}

TEST_CASE("run_init prompts for a standard it can't detect", "[init]") {
    const TempDir dir;
    dir.write("CMakeLists.txt", "project(plain CXX)\n");

    // c++ standard picker: 3 = c++17 (23, 20, 17, 14, 11).
    REQUIRE(run_init_in(dir, {}, "3\n").has_value());

    const auto proj = project::find_from(dir.path());
    REQUIRE(proj.has_value());
    REQUIRE(proj->config.cpp_standard == 17);
}

TEST_CASE("run_init refuses what it can't adopt", "[init]") {
    SECTION("an existing cup project") {
        const TempDir dir;
        dir.write("cup.toml", "name = \"already\"\n");
        const auto r = run_init_in(dir, {});
        REQUIRE_FALSE(r.has_value());
        REQUIRE(r.error().message().contains("already a cup project"));
    }
    SECTION("a modules-era standard on the Make backend") {
        const TempDir dir;
        dir.write("Makefile", "CXXFLAGS = -std=c++20\n");
        const auto r = run_init_in(dir, {});
        REQUIRE_FALSE(r.has_value());
        REQUIRE(r.error().message().contains("C++11/14/17 only"));
        REQUIRE_FALSE(std::filesystem::exists(dir.path() / "cup.toml"));
    }
    SECTION("bad flags and arguments") {
        const TempDir dir;
        dir.write("CMakeLists.txt", kExistingCMake);
        REQUIRE(run_init_in(dir, {"--bogus"}).error().message() == "unknown flag: --bogus");
        REQUIRE(run_init_in(dir, {"--std"}).error().message() == "--std needs a value");
        REQUIRE_FALSE(run_init_in(dir, {"--std", "98"}).has_value());
        REQUIRE(run_init_in(dir, {"--build-tool", "bazel"})
                    .error()
                    .message()
                    .contains("must be cmake or make"));
        REQUIRE(run_init_in(dir, {"a", "b"}).error().message() == "unexpected argument: b");
        REQUIRE_FALSE(run_init_in(dir, {"9lives"}).has_value());
        REQUIRE_FALSE(std::filesystem::exists(dir.path() / "cup.toml"));
    }
}
