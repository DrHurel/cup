#include <catch2/catch_test_macros.hpp>

#include <unistd.h>

#include <algorithm>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "TempDir.hpp"

import cup.cmd;
import cup.project;
import cup.scaffold;
import cup.tmpl;
import cup.platform;
import cup.ui;

namespace {

using cup::project::Config;
using cup::project::Project;
using cup::test::TempDir;

// Restores a mutable global (an overridable hook) to its prior value when the
// test ends, mirroring releases_test.cpp's ScopedOverride.
template <typename T>
class ScopedOverride {
public:
    ScopedOverride(T& slot, T value) : slot_(slot), previous_(slot) { slot_ = std::move(value); }
    ~ScopedOverride() { slot_ = std::move(previous_); }
    ScopedOverride(const ScopedOverride&) = delete;
    ScopedOverride& operator=(const ScopedOverride&) = delete;

private:
    T& slot_;
    T previous_;
};

// Points the real stdin fd at a pipe holding keys, so cup.ui's is_tty(kStdinFd)
// check sees "not a terminal" regardless of how the test binary was invoked.
// Mirrors ui_test.cpp's ScopedStdin.
class ScopedStdin {
public:
    explicit ScopedStdin(std::string_view keys) {
        int fds[2]{};
        REQUIRE(::pipe(fds) == 0);
        saved_ = ::dup(STDIN_FILENO);
        REQUIRE(saved_ >= 0);
        REQUIRE(::write(fds[1], keys.data(), keys.size()) == static_cast<ssize_t>(keys.size()));
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

// Changes the process's working directory for the test's lifetime, mirroring
// Go's t.Chdir -- run_new resolves a relative project name against cwd, the
// same as its Go original.
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

// Records every run_command call as "name arg1 arg2 ..."; unless told to
// fail (optionally only for a specific program name), reports success. The
// C++ analogue of helpers_test.go's stubRunCommand.
class StubRunCommand {
public:
    explicit StubRunCommand(std::string_view fail_for = {})
        : fail_for_(fail_for),
          override_(cup::platform::run_command_func(),
                    cup::platform::RunCommandFunc{
                        [this](const std::filesystem::path&, std::string_view name,
                              std::span<const std::string> args)
                            -> std::expected<void, cup::error::Error> {
                            std::string line(name);
                            for (const auto& a : args) {
                                line += ' ';
                                line += a;
                            }
                            calls_.push_back(line);
                            if (!fail_for_.empty() && name == fail_for_) {
                                return std::unexpected(cup::error::Error("stubbed failure"));
                            }
                            return {};
                        }}) {}

    [[nodiscard]] const std::vector<std::string>& calls() const { return calls_; }

private:
    std::string fail_for_;
    std::vector<std::string> calls_;
    ScopedOverride<cup::platform::RunCommandFunc> override_;
};

// newest_compilers_func can only be overridden with a plain function
// pointer (no captures), so the std it should answer for is threaded
// through a file-scope variable rather than a closure.
int g_stub_newest_std = 23;
std::pair<int, int> stub_newest_compilers() { return cup::scaffold::min_compilers(g_stub_newest_std); }

std::expected<cup::scaffold::Tags, cup::error::Error> stub_no_network(std::string_view) {
    return std::unexpected(cup::error::Error("stubbed: no network in tests"));
}

// scaffold_project drives run_new to build a real project fixture under
// dir/"demo": build_tool_choice/std_choice are the 1-based numbered answers
// to run_new's first two prompts (build system, C++ standard). Compiler
// floors are stubbed to cup's own curated default for std, so both the gcc
// and clang floor prompts auto-select (a lone choice) and need no answer.
// The base image is "debian" with Docker Hub unreachable (stubbed), so the
// tag prompt falls back to its "latest" default. git init runs for real.
Project scaffold_project(const TempDir& dir, std::string_view build_tool_choice, int std,
                         std::string_view std_choice) {
    g_stub_newest_std = std;
    const ScopedOverride newest_override(cup::scaffold::newest_compilers_func(),
                                         cup::scaffold::NewestCompilersFunc{&stub_newest_compilers});
    const ScopedOverride tags_override(cup::scaffold::docker_hub_tags_func(),
                                       cup::scaffold::DockerHubTagsFunc{&stub_no_network});

    const ScopedCwd cwd(dir.path());
    const ScopedStdin not_a_terminal("");
    std::istringstream in(std::string(build_tool_choice) + "\n" + std::string(std_choice) +
                          "\n1\ndebian\n\n");
    const cup::ui::ScopedInput scoped(in);

    auto result = cup::cmd::run_new(std::vector<std::string>{"demo"});
    REQUIRE(result.has_value());

    auto proj = cup::project::find_from(dir.path() / "demo");
    REQUIRE(proj.has_value());
    return *proj;
}

// index_of_kind returns the 1-based position of kind in family's available
// template kinds, for building a numbered-select feed line without hardcoding
// the corpus's kind ordering.
std::size_t index_of_kind(const std::filesystem::path& root, std::string_view family,
                          std::string_view kind) {
    const auto kinds = cup::tmpl::kinds(root, family);
    const auto it = std::find(kinds.begin(), kinds.end(), kind);
    REQUIRE(it != kinds.end());
    return static_cast<std::size_t>(std::distance(kinds.begin(), it)) + 1;
}

bool file_contains(const std::filesystem::path& path, std::string_view needle) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }
    const std::string content{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    return content.find(needle) != std::string::npos;
}

}

// --- new.go -----------------------------------------------------------------

TEST_CASE("resolve_project_name", "[cmd][new]") {
    SECTION("a valid name from args needs no prompt") {
        auto r = cup::cmd::resolve_project_name(std::vector<std::string>{"myproj"});
        REQUIRE(r.has_value());
        REQUIRE(*r == "myproj");
    }
    SECTION("an invalid name from args is rejected without prompting") {
        auto r = cup::cmd::resolve_project_name(std::vector<std::string>{"123bad"});
        REQUIRE_FALSE(r.has_value());
        REQUIRE(r.error().message().starts_with("project name \"123bad\""));
    }
    SECTION("no args prompts, then validates the answer") {
        const ScopedStdin not_a_terminal("");
        std::istringstream in("myproj\n");
        const cup::ui::ScopedInput scoped(in);
        auto r = cup::cmd::resolve_project_name({});
        REQUIRE(r.has_value());
        REQUIRE(*r == "myproj");
    }
}

TEST_CASE("standard_choices offers all standards for cmake, only the headers family for make",
          "[cmd][new]") {
    REQUIRE(cup::cmd::standard_choices("cmake") == std::vector<int>{23, 20, 17, 14, 11});
    REQUIRE(cup::cmd::standard_choices("make") == std::vector<int>{17, 14, 11});
}

TEST_CASE("choose_compiler_floor takes a lone choice without prompting", "[cmd][new]") {
    const std::vector<int> choices{15};
    auto r = cup::cmd::choose_compiler_floor("gcc", choices);
    REQUIRE(r.has_value());
    REQUIRE(*r == 15);
}

TEST_CASE("choose_compiler_floor prompts, oldest first and default, among several choices",
          "[cmd][new]") {
    const ScopedStdin not_a_terminal("");
    std::istringstream in("2\n");
    const cup::ui::ScopedInput scoped(in);
    const std::vector<int> choices{14, 15, 16};
    auto r = cup::cmd::choose_compiler_floor("gcc", choices);
    REQUIRE(r.has_value());
    REQUIRE(*r == 15);
}

TEST_CASE("module_std_setup", "[cmd][new]") {
    SECTION("the std module: CMake's experimental import-std opt-in") {
        const Config cfg{.cpp_standard = 23, .std_module = true};
        const auto out = cup::cmd::module_std_setup(cfg);
        REQUIRE(out.find("cmake_minimum_required(VERSION 3.30)") != std::string::npos);
        REQUIRE(out.find("f35a9ac6-8463-4d38-8eec-5d6008153e7d") != std::string::npos);
        REQUIRE(out.find("CMAKE_CXX_MODULE_STD ON") != std::string::npos);
    }
    SECTION("no std module: bare version floor") {
        const Config cfg{.cpp_standard = 23, .std_module = false};
        REQUIRE(cup::cmd::module_std_setup(cfg) ==
                "# Named modules need CMake >= 3.28.\ncmake_minimum_required(VERSION 3.28)\n");
    }
}

TEST_CASE("run_new (cmake, c++23) scaffolds a full project", "[cmd][new][end-to-end]") {
    const TempDir dir;
    const Project proj = scaffold_project(dir, "1", 23, "1");

    REQUIRE(proj.config.name == "demo");
    REQUIRE(proj.config.cpp_standard == 23);
    REQUIRE_FALSE(proj.uses_make());

    REQUIRE(file_contains(proj.root / "cup.toml", "cpp_standard = 23"));
    REQUIRE(file_contains(proj.root / "CMakeLists.txt", "demo"));
    REQUIRE(std::filesystem::exists(proj.root / ".gitignore"));
    REQUIRE(std::filesystem::exists(proj.root / "src" / "apps" / "CMakeLists.txt"));
    REQUIRE(std::filesystem::exists(proj.root / "src" / "libs" / "CMakeLists.txt"));
    REQUIRE(std::filesystem::exists(proj.root / ".git"));

    const auto* image = proj.config.docker.default_image();
    REQUIRE(image != nullptr);
    REQUIRE(image->base == "debian:latest");
    REQUIRE(file_contains(proj.root / "docker" / "demo" / "Dockerfile", "FROM debian:latest"));
}

TEST_CASE("run_new (make, c++17) scaffolds a Makefile project with no CMakeLists anywhere",
          "[cmd][new][end-to-end][make]") {
    const TempDir dir;
    const Project proj = scaffold_project(dir, "2", 17, "1");

    REQUIRE(proj.uses_make());
    REQUIRE(proj.config.cpp_standard == 17);
    REQUIRE(std::filesystem::exists(proj.root / "Makefile"));
    REQUIRE(file_contains(proj.root / "Makefile", "-std=c++17"));
    REQUIRE(file_contains(proj.root / ".gitignore", "/build/"));
    REQUIRE_FALSE(std::filesystem::exists(proj.root / "CMakeLists.txt"));
    REQUIRE_FALSE(std::filesystem::exists(proj.root / "src" / "apps" / "CMakeLists.txt"));
    REQUIRE(std::filesystem::is_directory(proj.root / "src" / "libs"));
}

TEST_CASE("run_new survives a failing git init", "[cmd][new]") {
    const TempDir dir;
    g_stub_newest_std = 23;
    const ScopedOverride newest_override(cup::scaffold::newest_compilers_func(),
                                         cup::scaffold::NewestCompilersFunc{&stub_newest_compilers});
    const ScopedOverride tags_override(cup::scaffold::docker_hub_tags_func(),
                                       cup::scaffold::DockerHubTagsFunc{&stub_no_network});
    const StubRunCommand git_fails("git");

    const ScopedCwd cwd(dir.path());
    const ScopedStdin not_a_terminal("");
    std::istringstream in("1\n1\n1\ndebian\n\n");
    const cup::ui::ScopedInput scoped(in);

    auto result = cup::cmd::run_new(std::vector<std::string>{"demo"});
    REQUIRE(result.has_value());
    REQUIRE(std::filesystem::exists(dir.path() / "demo" / "cup.toml"));
    REQUIRE_FALSE(std::filesystem::exists(dir.path() / "demo" / ".git"));
}

// --- add.go / add_headers.go -------------------------------------------------

TEST_CASE("primary_preamble carries the GCC 14 workaround exactly when there is no std module",
          "[cmd][add]") {
    REQUIRE(cup::cmd::primary_preamble(Config{.cpp_standard = 23, .std_module = false}) ==
            "module;\n#include <string>\n");
    REQUIRE(cup::cmd::primary_preamble(Config{.cpp_standard = 23, .std_module = true}).empty());
}

TEST_CASE("pick_or_new", "[cmd][add]") {
    SECTION("no options prompts straight for a new name") {
        const ScopedStdin not_a_terminal("");
        std::istringstream in("alpha\n");
        const cup::ui::ScopedInput scoped(in);
        auto r = cup::cmd::pick_or_new("lib name?", {}, "new lib name?", cup::scaffold::validate_ident);
        REQUIRE(r.has_value());
        REQUIRE(*r == "alpha");
    }
    SECTION("selecting an existing option returns it") {
        const ScopedStdin not_a_terminal("");
        std::istringstream in("1\n");
        const cup::ui::ScopedInput scoped(in);
        const std::vector<std::string> options{"alpha", "beta"};
        auto r = cup::cmd::pick_or_new("lib name?", options, "new lib name?", cup::scaffold::validate_ident);
        REQUIRE(r.has_value());
        REQUIRE(*r == "alpha");
    }
    SECTION("selecting the sentinel (always last) falls through to a new name") {
        const ScopedStdin not_a_terminal("");
        std::istringstream in("3\ngamma\n");
        const cup::ui::ScopedInput scoped(in);
        const std::vector<std::string> options{"alpha", "beta"};
        auto r = cup::cmd::pick_or_new("lib name?", options, "new lib name?", cup::scaffold::validate_ident);
        REQUIRE(r.has_value());
        REQUIRE(*r == "gamma");
    }
    SECTION("EOF on the picker aborts instead of falling through") {
        const ScopedStdin not_a_terminal("");
        std::istringstream in("");
        const cup::ui::ScopedInput scoped(in);
        const std::vector<std::string> options{"alpha", "beta"};
        auto r = cup::cmd::pick_or_new("lib name?", options, "new lib name?", cup::scaffold::validate_ident);
        REQUIRE_FALSE(r.has_value());
        REQUIRE(cup::error::is_abort(r.error()));
    }
}

TEST_CASE("choose_test_module prompts nothing when the project has no libs", "[cmd][add]") {
    const TempDir dir;
    const Project proj = scaffold_project(dir, "1", 23, "1");
    auto r = cup::cmd::choose_test_module(proj);
    REQUIRE(r.has_value());
    REQUIRE(r->empty());
}

TEST_CASE("test_module_import", "[cmd][add]") {
    const TempDir dir;
    const Project modules_proj = scaffold_project(dir, "1", 23, "1");
    REQUIRE(cup::cmd::test_module_import(modules_proj, "").empty());
    REQUIRE(cup::cmd::test_module_import(modules_proj, "utils") == "import utils;\n");

    const TempDir headers_dir;
    const Project headers_proj = scaffold_project(headers_dir, "1", 17, "3");
    REQUIRE(cup::cmd::test_module_import(headers_proj, "utils") == "#include \"utils.hpp\"\n");
}

TEST_CASE("add_app (cmake) writes a source, a CMakeLists, and registers the subdirectory",
          "[cmd][add][end-to-end]") {
    const TempDir dir;
    const Project proj = scaffold_project(dir, "1", 23, "1");

    const ScopedStdin not_a_terminal("");
    std::istringstream in("hello\n\n");
    const cup::ui::ScopedInput scoped(in);

    auto result = cup::cmd::add_app(proj);
    REQUIRE(result.has_value());

    const auto app_dir = proj.src() / "apps" / "hello";
    REQUIRE(std::filesystem::exists(app_dir / "hello.cpp"));
    REQUIRE(std::filesystem::exists(app_dir / "CMakeLists.txt"));
    REQUIRE(file_contains(proj.src() / "apps" / "CMakeLists.txt", "add_subdirectory(hello)"));
}

TEST_CASE("add_app (make) writes only the source, no CMakeLists anywhere", "[cmd][add][end-to-end][make]") {
    const TempDir dir;
    const Project proj = scaffold_project(dir, "2", 17, "1");

    const ScopedStdin not_a_terminal("");
    std::istringstream in("hello\n\n");
    const cup::ui::ScopedInput scoped(in);

    auto result = cup::cmd::add_app(proj);
    REQUIRE(result.has_value());

    const auto app_dir = proj.src() / "apps" / "hello";
    REQUIRE(std::filesystem::exists(app_dir / "hello.cpp"));
    REQUIRE_FALSE(std::filesystem::exists(app_dir / "CMakeLists.txt"));
}

TEST_CASE("add_lib (modules) creates a partitioned lib and registers it", "[cmd][add][end-to-end]") {
    const TempDir dir;
    const Project proj = scaffold_project(dir, "1", 23, "1");
    const std::size_t class_index = index_of_kind(proj.root, "modules", "class");

    const ScopedStdin not_a_terminal("");
    std::istringstream in("utils\n" + std::to_string(class_index) + "\n\n");
    const cup::ui::ScopedInput scoped(in);

    auto result = cup::cmd::add_lib(proj);
    REQUIRE(result.has_value());

    const auto lib_dir = proj.src() / "libs" / "utils";
    REQUIRE(std::filesystem::exists(lib_dir / "Utils.cppm"));
    REQUIRE(std::filesystem::exists(lib_dir / "utils.cppm"));
    REQUIRE(std::filesystem::exists(lib_dir / "CMakeLists.txt"));
    REQUIRE(file_contains(lib_dir / "utils.cppm", "export import :utils;"));
    REQUIRE(file_contains(proj.src() / "libs" / "CMakeLists.txt", "add_subdirectory(utils)"));
}

TEST_CASE("create_header_lib_at (headers family) creates a lib with a primary aggregator",
          "[cmd][add][end-to-end][headers]") {
    const TempDir dir;
    const Project proj = scaffold_project(dir, "1", 17, "1");
    const std::size_t class_index = index_of_kind(proj.root, "headers", "class");

    const ScopedStdin not_a_terminal("");
    std::istringstream in(std::to_string(class_index) + "\nUtils\n");
    const cup::ui::ScopedInput scoped(in);

    const auto lib_dir = proj.src() / "libs" / "utils";
    auto result = cup::cmd::create_header_lib_at(proj, "utils", lib_dir, proj.src() / "libs" / "CMakeLists.txt");
    REQUIRE(result.has_value());

    REQUIRE(std::filesystem::exists(lib_dir / "utils.hpp"));
    REQUIRE(file_contains(lib_dir / "utils.hpp", "#pragma once"));
    REQUIRE(std::filesystem::exists(lib_dir / "CMakeLists.txt"));
    REQUIRE(file_contains(proj.src() / "libs" / "CMakeLists.txt", "add_subdirectory(utils)"));
}

TEST_CASE("add_test (cmake) wires the test into src/tests/CMakeLists.txt and the root",
          "[cmd][add][end-to-end]") {
    const TempDir dir;
    const Project proj = scaffold_project(dir, "1", 23, "1");
    std::filesystem::create_directories(proj.src() / "tests");

    const ScopedStdin not_a_terminal("");
    std::istringstream in("smoke\n");
    const cup::ui::ScopedInput scoped(in);

    auto result = cup::cmd::add_test(proj);
    REQUIRE(result.has_value());

    REQUIRE(std::filesystem::exists(proj.src() / "tests" / "smoke.cpp"));
    REQUIRE(file_contains(proj.src() / "tests" / "CMakeLists.txt", "add_executable(smoke smoke.cpp)"));
    REQUIRE(file_contains(proj.src() / "tests" / "CMakeLists.txt", "add_test(NAME smoke COMMAND smoke)"));
    REQUIRE(file_contains(proj.root / "CMakeLists.txt", "add_subdirectory(src/tests)"));
}

// These exercise the "!x.has_value()) return unexpected(...)" propagation
// lines that follow every scaffold::write_file call: write_file itself only
// fails when its target already exists and the overwrite prompt is refused,
// so pre-creating the exact target and then starving stdin (EOF -> abort,
// same as a Ctrl-D) reaches that branch for real, without touching
// production code to add a test-only seam.

TEST_CASE("add_app reports a write failure when its CMakeLists exists and the overwrite "
          "prompt hits EOF",
          "[cmd][add]") {
    const TempDir dir;
    const Project proj = scaffold_project(dir, "1", 23, "1");
    const auto app_dir = proj.src() / "apps" / "hello";
    std::filesystem::create_directories(app_dir);
    std::ofstream(app_dir / "CMakeLists.txt") << "# stale\n";

    const ScopedStdin not_a_terminal("");
    std::istringstream in("hello\n\n");
    const cup::ui::ScopedInput scoped(in);

    auto result = cup::cmd::add_app(proj);
    REQUIRE_FALSE(result.has_value());
    REQUIRE(cup::error::is_abort(result.error()));
    REQUIRE(std::filesystem::exists(app_dir / "hello.cpp"));
}

TEST_CASE("create_lib_at reports a write failure when the primary aggregator exists and the "
          "overwrite prompt hits EOF",
          "[cmd][add]") {
    const TempDir dir;
    const Project proj = scaffold_project(dir, "1", 23, "1");
    const std::size_t class_index = index_of_kind(proj.root, "modules", "class");
    const auto lib_dir = proj.src() / "libs" / "utils";
    std::filesystem::create_directories(lib_dir);
    std::ofstream(lib_dir / "utils.cppm") << "// stale primary\n";

    const ScopedStdin not_a_terminal("");
    std::istringstream in(std::to_string(class_index) + "\nUtils\n");
    const cup::ui::ScopedInput scoped(in);

    auto result = cup::cmd::create_lib_at(proj, "utils", lib_dir, proj.src() / "libs" / "CMakeLists.txt");
    REQUIRE_FALSE(result.has_value());
    REQUIRE(cup::error::is_abort(result.error()));
    // The partition itself was written before the aggregator write failed.
    REQUIRE(std::filesystem::exists(lib_dir / "Utils.cppm"));
}

TEST_CASE("create_lib_at reports a write failure when the CMakeLists exists and the "
          "overwrite prompt hits EOF",
          "[cmd][add]") {
    const TempDir dir;
    const Project proj = scaffold_project(dir, "1", 23, "1");
    const std::size_t class_index = index_of_kind(proj.root, "modules", "class");
    const auto lib_dir = proj.src() / "libs" / "utils";
    std::filesystem::create_directories(lib_dir);
    std::ofstream(lib_dir / "CMakeLists.txt") << "# stale\n";

    const ScopedStdin not_a_terminal("");
    std::istringstream in(std::to_string(class_index) + "\nUtils\n");
    const cup::ui::ScopedInput scoped(in);

    auto result = cup::cmd::create_lib_at(proj, "utils", lib_dir, proj.src() / "libs" / "CMakeLists.txt");
    REQUIRE_FALSE(result.has_value());
    REQUIRE(cup::error::is_abort(result.error()));
    REQUIRE(std::filesystem::exists(lib_dir / "utils.cppm"));
}

TEST_CASE("add_file_to_lib reports a write failure when its component exists and the "
          "overwrite prompt hits EOF",
          "[cmd][add]") {
    const TempDir dir;
    const Project proj = scaffold_project(dir, "1", 23, "1");
    const std::size_t class_index = index_of_kind(proj.root, "modules", "class");
    const auto lib_dir = proj.src() / "libs" / "utils";
    {
        const ScopedStdin not_a_terminal("");
        std::istringstream setup_in(std::to_string(class_index) + "\nUtils\n");
        const cup::ui::ScopedInput setup_scoped(setup_in);
        REQUIRE(cup::cmd::create_lib_at(proj, "utils", lib_dir, proj.src() / "libs" / "CMakeLists.txt")
                    .has_value());
    }
    std::ofstream(lib_dir / "extra.cppm") << "// stale\n";

    const ScopedStdin not_a_terminal("");
    std::istringstream in("extra\n" + std::to_string(class_index) + "\nExtra\n");
    const cup::ui::ScopedInput scoped(in);

    auto result = cup::cmd::add_file_to_lib(proj, lib_dir);
    REQUIRE_FALSE(result.has_value());
    REQUIRE(cup::error::is_abort(result.error()));
}

TEST_CASE("create_header_lib_at reports a write failure when the CMakeLists exists and the "
          "overwrite prompt hits EOF",
          "[cmd][add][headers]") {
    const TempDir dir;
    const Project proj = scaffold_project(dir, "1", 17, "1");
    const std::size_t class_index = index_of_kind(proj.root, "headers", "class");
    const auto lib_dir = proj.src() / "libs" / "utils";
    std::filesystem::create_directories(lib_dir);
    std::ofstream(lib_dir / "CMakeLists.txt") << "# stale\n";

    const ScopedStdin not_a_terminal("");
    std::istringstream in(std::to_string(class_index) + "\nUtils\n");
    const cup::ui::ScopedInput scoped(in);

    auto result =
        cup::cmd::create_header_lib_at(proj, "utils", lib_dir, proj.src() / "libs" / "CMakeLists.txt");
    REQUIRE_FALSE(result.has_value());
    REQUIRE(cup::error::is_abort(result.error()));
}

TEST_CASE("add_file_to_header_lib reports a write failure when its header exists and the "
          "overwrite prompt hits EOF",
          "[cmd][add][headers]") {
    const TempDir dir;
    const Project proj = scaffold_project(dir, "1", 17, "1");
    const std::size_t class_index = index_of_kind(proj.root, "headers", "class");
    const auto lib_dir = proj.src() / "libs" / "utils";
    {
        const ScopedStdin not_a_terminal("");
        std::istringstream setup_in(std::to_string(class_index) + "\nUtils\n");
        const cup::ui::ScopedInput setup_scoped(setup_in);
        REQUIRE(
            cup::cmd::create_header_lib_at(proj, "utils", lib_dir, proj.src() / "libs" / "CMakeLists.txt")
                .has_value());
    }
    std::ofstream(lib_dir / "extra.h") << "// stale\n";

    const ScopedStdin not_a_terminal("");
    std::istringstream in("extra\n" + std::to_string(class_index) + "\nExtra\n");
    const cup::ui::ScopedInput scoped(in);

    auto result = cup::cmd::add_file_to_header_lib(proj, lib_dir);
    REQUIRE_FALSE(result.has_value());
    REQUIRE(cup::error::is_abort(result.error()));
}

TEST_CASE("add_test reports a write failure when its source exists and the overwrite prompt "
          "hits EOF",
          "[cmd][add]") {
    const TempDir dir;
    const Project proj = scaffold_project(dir, "1", 23, "1");
    std::filesystem::create_directories(proj.src() / "tests");
    std::ofstream(proj.src() / "tests" / "smoke.cpp") << "// stale\n";

    const ScopedStdin not_a_terminal("");
    std::istringstream in("smoke\n");
    const cup::ui::ScopedInput scoped(in);

    auto result = cup::cmd::add_test(proj);
    REQUIRE_FALSE(result.has_value());
    REQUIRE(cup::error::is_abort(result.error()));
}

TEST_CASE("run_new refuses to overwrite a directory that already exists", "[cmd][new]") {
    const TempDir dir;
    std::filesystem::create_directories(dir.path() / "demo");

    g_stub_newest_std = 23;
    const ScopedOverride newest_override(cup::scaffold::newest_compilers_func(),
                                         cup::scaffold::NewestCompilersFunc{&stub_newest_compilers});
    const ScopedOverride tags_override(cup::scaffold::docker_hub_tags_func(),
                                       cup::scaffold::DockerHubTagsFunc{&stub_no_network});
    const ScopedCwd cwd(dir.path());
    const ScopedStdin not_a_terminal("");
    std::istringstream in("demo\n1\n1\n1\ndebian\n\n");
    const cup::ui::ScopedInput scoped(in);

    auto result = cup::cmd::run_new({});
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().message() == "demo already exists");
}

TEST_CASE("scaffold_project_tree reports a write failure when CMakeLists.txt exists and the "
          "overwrite prompt hits EOF",
          "[cmd][new]") {
    const TempDir dir;
    const Project proj{dir.path(),
                       Config{.name = "demo",
                              .cpp_standard = 23,
                              .build_tool = "cmake",
                              .compiler = cup::project::make_compiler_config(15, 18)}};
    std::filesystem::create_directories(proj.root);
    std::ofstream(proj.root / "CMakeLists.txt") << "# stale\n";

    const ScopedStdin not_a_terminal("");
    std::istringstream in("");
    const cup::ui::ScopedInput scoped(in);

    auto result = cup::cmd::scaffold_project_tree(proj, 23, 15, 18);
    REQUIRE_FALSE(result.has_value());
    REQUIRE(cup::error::is_abort(result.error()));
}

TEST_CASE("scaffold_project_tree reports a write failure when .gitignore exists and the "
          "overwrite prompt hits EOF",
          "[cmd][new]") {
    const TempDir dir;
    const Project proj{dir.path(),
                       Config{.name = "demo",
                              .cpp_standard = 23,
                              .build_tool = "cmake",
                              .compiler = cup::project::make_compiler_config(15, 18)}};
    std::filesystem::create_directories(proj.root);
    std::ofstream(proj.root / ".gitignore") << "# stale\n";

    const ScopedStdin not_a_terminal("");
    std::istringstream in("");
    const cup::ui::ScopedInput scoped(in);

    auto result = cup::cmd::scaffold_project_tree(proj, 23, 15, 18);
    REQUIRE_FALSE(result.has_value());
    REQUIRE(cup::error::is_abort(result.error()));
    REQUIRE(std::filesystem::exists(proj.root / "CMakeLists.txt"));
}

TEST_CASE("scaffold_make_tree reports a write failure when the Makefile exists and the "
          "overwrite prompt hits EOF",
          "[cmd][new]") {
    const TempDir dir;
    const Project proj{
        dir.path(), Config{.name = "demo", .cpp_standard = 17, .build_tool = "make"}};
    std::filesystem::create_directories(proj.root);
    std::ofstream(proj.root / "Makefile") << "# stale\n";

    const ScopedStdin not_a_terminal("");
    std::istringstream in("");
    const cup::ui::ScopedInput scoped(in);

    auto result = cup::cmd::scaffold_make_tree(proj, 17);
    REQUIRE_FALSE(result.has_value());
    REQUIRE(cup::error::is_abort(result.error()));
}

TEST_CASE("scaffold_make_tree reports a write failure when .gitignore exists and the "
          "overwrite prompt hits EOF",
          "[cmd][new]") {
    const TempDir dir;
    const Project proj{
        dir.path(), Config{.name = "demo", .cpp_standard = 17, .build_tool = "make"}};
    std::filesystem::create_directories(proj.root);
    std::ofstream(proj.root / ".gitignore") << "# stale\n";

    const ScopedStdin not_a_terminal("");
    std::istringstream in("");
    const cup::ui::ScopedInput scoped(in);

    auto result = cup::cmd::scaffold_make_tree(proj, 17);
    REQUIRE_FALSE(result.has_value());
    REQUIRE(cup::error::is_abort(result.error()));
    REQUIRE(std::filesystem::exists(proj.root / "Makefile"));
}

TEST_CASE("run_new (c++23, std_module = false) never writes import std;", "[cmd][add][end-to-end]") {
    const TempDir dir;
    Project proj = scaffold_project(dir, "1", 23, "1");

    // No picker sets std_module yet (matches the Go source), so flip it by
    // hand and re-persist, the way cup's own cup.toml itself does.
    proj.config.std_module = false;
    REQUIRE(cup::project::write_config(proj.root, proj.config).has_value());

    {
        const ScopedStdin not_a_terminal("");
        std::istringstream in("hello\n\n");
        const cup::ui::ScopedInput scoped(in);
        REQUIRE(cup::cmd::add_app(proj).has_value());
    }
    {
        const std::size_t class_index = index_of_kind(proj.root, "modules", "class");
        const ScopedStdin not_a_terminal("");
        std::istringstream in("utils\n" + std::to_string(class_index) + "\n\n");
        const cup::ui::ScopedInput scoped(in);
        REQUIRE(cup::cmd::add_lib(proj).has_value());
    }

    for (const auto& file : {proj.src() / "apps" / "hello" / "hello.cpp", proj.src() / "libs" / "utils" / "Utils.cppm",
                             proj.src() / "libs" / "utils" / "utils.cppm"}) {
        REQUIRE_FALSE(file_contains(file, "import std;"));
    }
    REQUIRE(file_contains(proj.src() / "libs" / "utils" / "utils.cppm", "module;\n#include <string>\n"));
    REQUIRE(file_contains(proj.src() / "libs" / "utils" / "Utils.cppm", "#include <print>"));
}

TEST_CASE("run_add dispatches an explicit category without prompting",
          "[cmd][add][end-to-end]") {
    const TempDir dir;
    const Project proj = scaffold_project(dir, "1", 23, "1");
    const ScopedCwd cwd(proj.root);

    const ScopedStdin not_a_terminal("");
    std::istringstream in("hello\n\n");
    const cup::ui::ScopedInput scoped(in);

    auto result = cup::cmd::run_add(std::vector<std::string>{"app"});
    REQUIRE(result.has_value());
    REQUIRE(std::filesystem::exists(proj.src() / "apps" / "hello" / "hello.cpp"));
}

TEST_CASE("run_add routes the third-party category to cup register, and rejects an "
          "unknown category",
          "[cmd][add]") {
    const TempDir dir;
    const Project proj = scaffold_project(dir, "1", 23, "1");
    const ScopedCwd cwd(proj.root);

    // method=apt-install(3), find_package name, apt package default, decline
    // the install prompt — exercises run_add's own dispatch responsibility
    // (routing to run_register), not register_apt's internals, which
    // thirdparty_cmd_test.cpp already covers directly.
    const ScopedStdin not_a_terminal("");
    std::istringstream in("3\nBoost\n\nn\n");
    const cup::ui::ScopedInput scoped(in);
    auto third_party = cup::cmd::run_add(std::vector<std::string>{"third-party"});
    REQUIRE(third_party.has_value());
    REQUIRE(file_contains(proj.root / "third_party" / "CMakeLists.txt", "find_package(Boost REQUIRED)"));

    auto unknown = cup::cmd::run_add(std::vector<std::string>{"bogus"});
    REQUIRE_FALSE(unknown.has_value());
}

TEST_CASE("run_add prompts for a category, then offers to add another",
          "[cmd][add][end-to-end]") {
    const TempDir dir;
    const Project proj = scaffold_project(dir, "1", 23, "1");
    const ScopedCwd cwd(proj.root);

    const ScopedStdin not_a_terminal("");
    // category=app(1), app name, source filename (blank -> default), then
    // "add another?" answered no.
    std::istringstream in("1\nhello\n\nn\n");
    const cup::ui::ScopedInput scoped(in);

    auto result = cup::cmd::run_add({});
    REQUIRE(result.has_value());
    REQUIRE(std::filesystem::exists(proj.src() / "apps" / "hello" / "hello.cpp"));
}

TEST_CASE("add_file_to_lib adds a second component to an existing modules lib",
          "[cmd][add][end-to-end]") {
    const TempDir dir;
    const Project proj = scaffold_project(dir, "1", 23, "1");
    const std::size_t class_index = index_of_kind(proj.root, "modules", "class");

    {
        const ScopedStdin not_a_terminal("");
        std::istringstream in("utils\n" + std::to_string(class_index) + "\n\n");
        const cup::ui::ScopedInput scoped(in);
        REQUIRE(cup::cmd::add_lib(proj).has_value());
    }

    const auto lib_dir = proj.src() / "libs" / "utils";
    {
        const ScopedStdin not_a_terminal("");
        std::istringstream in("json\n" + std::to_string(class_index) + "\n\n");
        const cup::ui::ScopedInput scoped(in);
        REQUIRE(cup::cmd::add_file_to_lib(proj, lib_dir).has_value());
    }

    REQUIRE(std::filesystem::exists(lib_dir / "json.cppm"));
    REQUIRE(file_contains(lib_dir / "utils.cppm", "export import :json;"));
    REQUIRE(file_contains(lib_dir / "CMakeLists.txt", "json.cppm"));
}

TEST_CASE("extend_lib: file adds a component, subfolder recurses into a nested lib",
          "[cmd][add][end-to-end]") {
    const TempDir dir;
    const Project proj = scaffold_project(dir, "1", 23, "1");
    const std::size_t class_index = index_of_kind(proj.root, "modules", "class");

    {
        const ScopedStdin not_a_terminal("");
        std::istringstream in("utils\n" + std::to_string(class_index) + "\n\n");
        const cup::ui::ScopedInput scoped(in);
        REQUIRE(cup::cmd::add_lib(proj).has_value());
    }
    const auto lib_dir = proj.src() / "libs" / "utils";

    SECTION("file") {
        const ScopedStdin not_a_terminal("");
        std::istringstream in("1\njson\n" + std::to_string(class_index) + "\n\n");
        const cup::ui::ScopedInput scoped(in);
        REQUIRE(cup::cmd::extend_lib(proj, lib_dir).has_value());
        REQUIRE(std::filesystem::exists(lib_dir / "json.cppm"));
    }

    SECTION("subfolder") {
        const ScopedStdin not_a_terminal("");
        // extend-as=subfolder(2); no existing subfolders, so pick_or_new goes
        // straight to a text prompt for the name; then createLibAt's own kind
        // + symbol (blank -> default) prompts for the nested lib.
        std::istringstream in("2\nnested\n" + std::to_string(class_index) + "\n\n");
        const cup::ui::ScopedInput scoped(in);
        REQUIRE(cup::cmd::extend_lib(proj, lib_dir).has_value());
        REQUIRE(std::filesystem::exists(lib_dir / "nested" / "nested.cppm"));
        REQUIRE(file_contains(lib_dir / "CMakeLists.txt", "add_subdirectory(nested)"));
    }

    SECTION("an already-existing subfolder recurses into it instead of recreating it") {
        {
            const ScopedStdin not_a_terminal("");
            std::istringstream in("2\nnested\n" + std::to_string(class_index) + "\n\n");
            const cup::ui::ScopedInput scoped(in);
            REQUIRE(cup::cmd::extend_lib(proj, lib_dir).has_value());
        }

        const ScopedStdin not_a_terminal("");
        // extend-as=subfolder(2); "nested" already exists, so pick_or_new
        // offers it as option 1 instead of prompting for a new name; then
        // extend_lib recurses into it and adds a file there.
        std::istringstream in("2\n1\n1\ninner\n" + std::to_string(class_index) + "\n\n");
        const cup::ui::ScopedInput scoped(in);
        REQUIRE(cup::cmd::extend_lib(proj, lib_dir).has_value());
        REQUIRE(std::filesystem::exists(lib_dir / "nested" / "inner.cppm"));
    }
}

TEST_CASE("add_lib on a headers-family project delegates through create_lib_at",
          "[cmd][add][end-to-end][headers]") {
    const TempDir dir;
    // std_choice "3", not "1": cmake's standard list is [23,20,17,14,11], and
    // this test genuinely needs !proj.uses_modules() to exercise
    // create_lib_at's own header-family dispatch branch (unlike the other
    // headers-family tests here, which call create_header_lib_at directly
    // and so don't depend on the fixture project's actual standard).
    const Project proj = scaffold_project(dir, "1", 17, "3");
    const std::size_t class_index = index_of_kind(proj.root, "headers", "class");

    const ScopedStdin not_a_terminal("");
    std::istringstream in("utils\n" + std::to_string(class_index) + "\n\n");
    const cup::ui::ScopedInput scoped(in);

    auto result = cup::cmd::add_lib(proj);
    REQUIRE(result.has_value());
    REQUIRE(std::filesystem::exists(proj.src() / "libs" / "utils" / "utils.hpp"));
}

TEST_CASE("add_file_to_header_lib adds a second component to an existing header lib",
          "[cmd][add][end-to-end][headers]") {
    const TempDir dir;
    const Project proj = scaffold_project(dir, "1", 17, "1");
    const std::size_t class_index = index_of_kind(proj.root, "headers", "class");

    const auto lib_dir = proj.src() / "libs" / "utils";
    {
        const ScopedStdin not_a_terminal("");
        std::istringstream in(std::to_string(class_index) + "\nUtils\n");
        const cup::ui::ScopedInput scoped(in);
        REQUIRE(cup::cmd::create_header_lib_at(proj, "utils", lib_dir,
                                               proj.src() / "libs" / "CMakeLists.txt")
                    .has_value());
    }
    {
        const ScopedStdin not_a_terminal("");
        std::istringstream in("json\n" + std::to_string(class_index) + "\n\n");
        const cup::ui::ScopedInput scoped(in);
        REQUIRE(cup::cmd::add_file_to_header_lib(proj, lib_dir).has_value());
    }

    REQUIRE(std::filesystem::exists(lib_dir / "json.h"));
    REQUIRE(std::filesystem::exists(lib_dir / "json.cpp"));
    REQUIRE(file_contains(lib_dir / "utils.hpp", "#include \"json.h\""));
    REQUIRE(file_contains(lib_dir / "CMakeLists.txt", "json.h"));
}

TEST_CASE("create_header_lib_at under Make writes no CMakeLists and no parent registration",
          "[cmd][add][end-to-end][make]") {
    const TempDir dir;
    const Project proj = scaffold_project(dir, "2", 17, "1");
    const std::size_t class_index = index_of_kind(proj.root, "headers", "class");

    const ScopedStdin not_a_terminal("");
    std::istringstream in(std::to_string(class_index) + "\n\n");
    const cup::ui::ScopedInput scoped(in);

    const auto lib_dir = proj.src() / "libs" / "utils";
    auto result = cup::cmd::create_header_lib_at(proj, "utils", lib_dir,
                                                  proj.src() / "libs" / "CMakeLists.txt");
    REQUIRE(result.has_value());
    REQUIRE(std::filesystem::exists(lib_dir / "utils.hpp"));
    REQUIRE_FALSE(std::filesystem::exists(lib_dir / "CMakeLists.txt"));
}

TEST_CASE("choose_test_module prompts and returns the chosen lib, or \"\" for [none]",
          "[cmd][add]") {
    const TempDir dir;
    const Project proj = scaffold_project(dir, "1", 23, "1");
    std::filesystem::create_directories(proj.src() / "libs" / "utils");

    SECTION("choosing the lib") {
        const ScopedStdin not_a_terminal("");
        std::istringstream in("2\n");
        const cup::ui::ScopedInput scoped(in);
        auto module = cup::cmd::choose_test_module(proj);
        REQUIRE(module.has_value());
        REQUIRE(*module == "utils");
    }
    SECTION("choosing [none]") {
        const ScopedStdin not_a_terminal("");
        std::istringstream in("1\n");
        const cup::ui::ScopedInput scoped(in);
        auto module = cup::cmd::choose_test_module(proj);
        REQUIRE(module.has_value());
        REQUIRE(module->empty());
    }
}
