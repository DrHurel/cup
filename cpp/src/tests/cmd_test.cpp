#include <catch2/catch_test_macros.hpp>

#include <unistd.h>

#include <expected>
#include <filesystem>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "TempDir.hpp"

import cup.cmd;
import cup.project;
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
// Go's t.Chdir -- run_configure/run_build/.../run_clean all locate the
// project via project::find() from cwd, same as their Go originals.
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

// Records every run_command call as "name arg1 arg2 ..." and, unless told to
// fail, reports success -- the C++ analogue of helpers_test.go's
// stubRunCommand.
class StubRunCommand {
public:
    explicit StubRunCommand(bool fail = false)
        : fail_(fail),
          override_(cup::platform::run_command_func(),
                    cup::platform::RunCommandFunc{
                        [this](const std::filesystem::path&, std::string_view name,
                              std::span<const std::string> args)
                            -> std::expected<void, cup::error::Error> {
                            std::string line(name);
                            for (const auto& arg : args) {
                                line += ' ';
                                line += arg;
                            }
                            calls_.push_back(std::move(line));
                            if (fail_) {
                                return std::unexpected(cup::error::Error("stubbed failure"));
                            }
                            return {};
                        }}) {}

    [[nodiscard]] const std::vector<std::string>& calls() const { return calls_; }

private:
    bool fail_;
    std::vector<std::string> calls_;
    ScopedOverride<cup::platform::RunCommandFunc> override_;
};

// Writes a minimal cup.toml under dir so project::find() (cwd-based, exactly
// like its Go original) can locate it. tool selects cmake ("") or make.
Project make_project(const TempDir& dir, std::string_view tool = "") {
    Config cfg{.name = "demo", .cpp_standard = 23, .build_tool = std::string(tool)};
    REQUIRE(cup::project::write_config(dir.path(), cfg).has_value());
    return Project{dir.path(), cfg};
}

}

TEST_CASE("parse_mode defaults to Debug and only consumes a recognised mode token",
          "[cmd][parse_mode]") {
    using cup::cmd::parse_mode;

    {
        const std::vector<std::string> args;
        const auto [mode, rest] = parse_mode(args);
        REQUIRE(mode == "Debug");
        REQUIRE(rest.empty());
    }
    {
        const std::vector<std::string> args{"Release"};
        const auto [mode, rest] = parse_mode(args);
        REQUIRE(mode == "Release");
        REQUIRE(rest.empty());
    }
    {
        const std::vector<std::string> args{"Coverage", "app"};
        const auto [mode, rest] = parse_mode(args);
        REQUIRE(mode == "Coverage");
        REQUIRE(rest.size() == 1);
        REQUIRE(rest[0] == "app");
    }
    {
        // A non-mode first token is left in place rather than consumed.
        const std::vector<std::string> args{"app"};
        const auto [mode, rest] = parse_mode(args);
        REQUIRE(mode == "Debug");
        REQUIRE(rest.size() == 1);
        REQUIRE(rest[0] == "app");
    }
}

TEST_CASE("build_dir joins root, build, and the mode", "[cmd][build_dir]") {
    const TempDir dir;
    const Project proj = make_project(dir);
    REQUIRE(cup::cmd::build_dir(proj, "Release") == dir.path() / "build" / "Release");
}

TEST_CASE("resolve_app", "[cmd][resolve_app]") {
    const TempDir dir;
    const Project proj = make_project(dir);

    SECTION("no apps is an error") {
        const auto resolved = cup::cmd::resolve_app(proj, {});
        REQUIRE_FALSE(resolved.has_value());
        REQUIRE(resolved.error().message() == "no apps to run (add one with `cup add app`)");
        return;
    }

    std::filesystem::create_directories(proj.src() / "apps" / "greeter");

    SECTION("one app, no explicit name -> that app; a leading -- separates program args") {
        const auto resolved = cup::cmd::resolve_app(proj, std::vector<std::string>{"--", "-v"});
        REQUIRE(resolved.has_value());
        REQUIRE(resolved->first == "greeter");
        REQUIRE(resolved->second == std::vector<std::string>{"-v"});
    }

    SECTION("an explicit leading name is taken as the app") {
        const auto resolved = cup::cmd::resolve_app(proj, std::vector<std::string>{"greeter"});
        REQUIRE(resolved.has_value());
        REQUIRE(resolved->first == "greeter");
        REQUIRE(resolved->second.empty());
    }

    SECTION("two apps, no name -> prompted; a numbered answer off a terminal picks by index") {
        std::filesystem::create_directories(proj.src() / "apps" / "other");

        const ScopedStdin not_a_terminal("");
        std::istringstream in("2\n");
        const cup::ui::ScopedInput scoped(in);

        const auto resolved = cup::cmd::resolve_app(proj, {});
        REQUIRE(resolved.has_value());
        REQUIRE((resolved->first == "greeter" || resolved->first == "other"));
    }
}

TEST_CASE("run_clean removes build/ and reports success outside a build tree",
          "[cmd][clean]") {
    const TempDir dir;
    make_project(dir);
    std::filesystem::create_directories(dir.path() / "build" / "Debug");
    const ScopedCwd cwd(dir.path());

    const auto result = cup::cmd::run_clean({});
    REQUIRE(result.has_value());
    REQUIRE_FALSE(std::filesystem::exists(dir.path() / "build"));
}

TEST_CASE("run_clean outside a project reports project::find's error", "[cmd][clean]") {
    const TempDir dir;
    const ScopedCwd cwd(dir.path());

    const auto result = cup::cmd::run_clean({});
    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("run_build dispatches straight to make for a make project", "[cmd][build][make]") {
    const TempDir dir;
    make_project(dir, "make");
    const ScopedCwd cwd(dir.path());
    StubRunCommand stub;

    const auto result = cup::cmd::run_build({});
    REQUIRE(result.has_value());
    REQUIRE(stub.calls() == std::vector<std::string>{"make MODE=Debug"});
}

TEST_CASE("run_test dispatches straight to make for a make project", "[cmd][test][make]") {
    const TempDir dir;
    make_project(dir, "make");
    const ScopedCwd cwd(dir.path());
    StubRunCommand stub;

    const auto result = cup::cmd::run_test(std::vector<std::string>{"Release"});
    REQUIRE(result.has_value());
    REQUIRE(stub.calls() == std::vector<std::string>{"make MODE=Release test"});
}

TEST_CASE("run_build configures then builds a cmake project", "[cmd][build][cmake]") {
    const TempDir dir;
    make_project(dir);
    const ScopedCwd cwd(dir.path());
    StubRunCommand stub;

    const auto result = cup::cmd::run_build({});
    REQUIRE(result.has_value());
    REQUIRE(stub.calls().size() == 2);
    REQUIRE(stub.calls()[0].starts_with("cmake -G Ninja -DCMAKE_BUILD_TYPE=Debug"));
    REQUIRE(stub.calls()[1] == "cmake --build " + (dir.path() / "build" / "Debug").string());
}

TEST_CASE("run_rebuild cleans then builds", "[cmd][rebuild]") {
    const TempDir dir;
    make_project(dir, "make");
    std::filesystem::create_directories(dir.path() / "build" / "Debug");
    const ScopedCwd cwd(dir.path());
    StubRunCommand stub;

    const auto result = cup::cmd::run_rebuild({});
    REQUIRE(result.has_value());
    REQUIRE_FALSE(std::filesystem::exists(dir.path() / "build" / "Debug"));
    REQUIRE(stub.calls() == std::vector<std::string>{"make MODE=Debug"});
}

TEST_CASE("run_run resolves the app, builds, then runs the binary", "[cmd][run]") {
    const TempDir dir;
    make_project(dir, "make");
    std::filesystem::create_directories(dir.path() / "src" / "apps" / "greeter");
    const ScopedCwd cwd(dir.path());
    StubRunCommand stub;

    const auto result = cup::cmd::run_run(std::vector<std::string>{"--", "-v"});
    REQUIRE(result.has_value());
    REQUIRE(stub.calls().size() == 2);
    REQUIRE(stub.calls()[0] == "make MODE=Debug");
    REQUIRE(stub.calls()[1] ==
            (dir.path() / "build" / "Debug" / "bin" / "greeter").string() + " -v");
}

TEST_CASE("commands lists the ported commands in cup's order", "[cmd][dispatch]") {
    const auto cmds = cup::cmd::commands();
    const std::vector<std::string> names{"new",  "add",     "configure", "build", "rebuild",
                                         "run",  "test",    "retest",    "clean"};
    REQUIRE(cmds.size() == names.size());
    for (std::size_t i = 0; i < names.size(); ++i) {
        REQUIRE(cmds[i].name == names[i]);
        REQUIRE_FALSE(cmds[i].summary.empty());
        REQUIRE(static_cast<bool>(cmds[i].run));
    }
}
