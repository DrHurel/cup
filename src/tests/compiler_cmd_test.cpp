#include <catch2/catch_test_macros.hpp>

#include <expected>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "TempDir.hpp"

import cup.cmd;
import cup.project;
import cup.scaffold;
import cup.platform;
import cup.error;

namespace {

using cup::project::Config;
using cup::project::DockerConfig;
using cup::project::DockerImage;
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
// fail, reports success. The C++ analogue of helpers_test.go's stubRunCommand.
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

std::optional<std::string> read_whole_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    return std::string{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

// Writes a minimal cup.toml under dir so project::find() can locate it, and a
// root CMakeLists carrying a compiler-guard block so apply_compiler_floor has
// markers to rewrite — mirrors compiler_test.go's newProject + seedGuardedCMake.
Project make_project(const TempDir& dir, int gcc, int clang, const DockerConfig& docker = {}) {
    Config cfg{.name = "demo", .cpp_standard = 20, .build_tool = "cmake", .docker = docker};
    REQUIRE(cup::project::write_config(dir.path(), cfg).has_value());
    const std::string body = "project(demo VERSION 0.1.0 LANGUAGES C CXX)\n\n" +
                             cup::scaffold::compiler_guard(gcc, clang) +
                             "\n\nset(CMAKE_CXX_STANDARD 20)\n";
    dir.write("CMakeLists.txt", body);
    return Project{dir.path(), cfg};
}

// Same as make_project, but for a Make-tooled project: no CMakeLists.txt is
// ever written, mirroring what `cup new` produces when build_tool = "make".
Project make_project_make(const TempDir& dir) {
    Config cfg{.name = "demo", .cpp_standard = 20, .build_tool = "make"};
    REQUIRE(cup::project::write_config(dir.path(), cfg).has_value());
    return Project{dir.path(), cfg};
}

}

TEST_CASE("effective_compilers falls back to per-standard defaults with no floor",
          "[cmd][compiler]") {
    const auto [gcc, clang] = cup::cmd::effective_compilers(Config{.cpp_standard = 20});
    REQUIRE(gcc == 11);
    REQUIRE(clang == 16);
}

TEST_CASE("effective_compilers prefers an explicit (even partial) floor", "[cmd][compiler]") {
    const auto [gcc, clang] = cup::cmd::effective_compilers(
        Config{.cpp_standard = 23, .compiler = cup::project::make_compiler_config(14, 0)});
    REQUIRE(gcc == 14);
    REQUIRE(clang == 0);
}

TEST_CASE("floor_label", "[cmd][compiler]") {
    REQUIRE(cup::cmd::floor_label(0) == "(no floor)");
    REQUIRE(cup::cmd::floor_label(15) == ">= 15");
}

TEST_CASE("parse_compiler_flags", "[cmd][compiler]") {
    SECTION("peels --image and --no-verify out, in any position") {
        const std::vector<std::string> args{"gcc", "15", "--image", "cxx:15", "--no-verify"};
        auto parsed = cup::cmd::parse_compiler_flags(args);
        REQUIRE(parsed.has_value());
        REQUIRE(parsed->image == "cxx:15");
        REQUIRE(parsed->no_verify);
        REQUIRE(parsed->rest == std::vector<std::string>{"gcc", "15"});
    }
    SECTION("--image without a value is an error") {
        const std::vector<std::string> args{"--image"};
        REQUIRE_FALSE(cup::cmd::parse_compiler_flags(args).has_value());
    }
}

TEST_CASE("plan_compiler_change", "[cmd][compiler]") {
    const Config cur{.cpp_standard = 20};

    SECTION("materialises the other compiler's effective default") {
        auto planned = cup::cmd::plan_compiler_change(cur, std::vector<std::string>{"gcc", "15"});
        REQUIRE(planned.has_value());
        REQUIRE(planned->name == "gcc");
        REQUIRE(planned->version == 15);
        REQUIRE(planned->config.compiler.gcc_floor() == 15);
        REQUIRE(planned->config.compiler.clang_floor() == 16);
    }
    SECTION("an unknown compiler name is rejected") {
        REQUIRE_FALSE(
            cup::cmd::plan_compiler_change(cur, std::vector<std::string>{"rustc", "1"}).has_value());
    }
    SECTION("a non-numeric version is rejected") {
        REQUIRE_FALSE(
            cup::cmd::plan_compiler_change(cur, std::vector<std::string>{"gcc", "notanumber"})
                .has_value());
    }
    SECTION("a negative version is rejected") {
        REQUIRE_FALSE(
            cup::cmd::plan_compiler_change(cur, std::vector<std::string>{"gcc", "-1"}).has_value());
    }
    SECTION("the wrong number of positional args is rejected") {
        REQUIRE_FALSE(cup::cmd::plan_compiler_change(cur, std::vector<std::string>{"gcc"}).has_value());
    }
    SECTION("preserves an existing verify_image") {
        Config with_image = cur;
        with_image.compiler.verify_image = "cxx:14";
        auto planned = cup::cmd::plan_compiler_change(with_image, std::vector<std::string>{"clang", "18"});
        REQUIRE(planned.has_value());
        REQUIRE(planned->config.compiler.verify_image == "cxx:14");
    }
}

TEST_CASE("has_verify_target", "[cmd][compiler]") {
    const TempDir dir;
    const Project proj = make_project(dir, 11, 16);

    REQUIRE_FALSE(cup::cmd::has_verify_target(proj, ""));
    REQUIRE(cup::cmd::has_verify_target(proj, "cxx:15"));

    const DockerConfig docker{
        .images = {DockerImage{.name = "demo", .base = "gcc:14", .is_default = true}}};
    const Project with_image = make_project(dir, 11, 16, docker);
    REQUIRE(cup::cmd::has_verify_target(with_image, ""));
}

TEST_CASE("show_compilers reports without touching docker", "[cmd][compiler]") {
    const TempDir dir;
    Project proj = make_project(dir, 11, 16);
    REQUIRE(cup::cmd::show_compilers(proj).has_value());

    proj.config.compiler = cup::project::make_compiler_config(14, 0);
    proj.config.compiler.verify_image = "cxx:14";
    REQUIRE(cup::cmd::show_compilers(proj).has_value());
}

TEST_CASE("resolve_verify_image prefers an explicit override", "[cmd][compiler]") {
    const TempDir dir;
    const Project proj = make_project(dir, 11, 16);
    auto got = cup::cmd::resolve_verify_image(proj, "cxx:15");
    REQUIRE(got.has_value());
    REQUIRE(*got == "cxx:15");
}

TEST_CASE("resolve_verify_image errors with no override, no default image, and no verify_image",
          "[cmd][compiler]") {
    const TempDir dir;
    const Project proj = make_project(dir, 11, 16);
    REQUIRE_FALSE(cup::cmd::resolve_verify_image(proj, "").has_value());
}

TEST_CASE("resolve_verify_image uses a legacy verify_image directly", "[cmd][compiler]") {
    const TempDir dir;
    Project proj = make_project(dir, 11, 16);
    proj.config.compiler.verify_image = "cup-cxx:latest";
    auto got = cup::cmd::resolve_verify_image(proj, "");
    REQUIRE(got.has_value());
    REQUIRE(*got == "cup-cxx:latest");
}

TEST_CASE("resolve_verify_image builds the default image, picking up apt dependencies",
          "[cmd][compiler]") {
    const TempDir dir;
    const DockerConfig docker{
        .images = {DockerImage{.name = "demo", .base = "gcc:14", .is_default = true}}};
    const Project proj = make_project(dir, 11, 16, docker);
    StubRunCommand stub;

    auto got = cup::cmd::resolve_verify_image(proj, "");
    REQUIRE(got.has_value());
    REQUIRE(*got == "demo:latest");

    const auto content = read_whole_file(cup::cmd::dockerfile_path(proj, "demo"));
    REQUIRE(content.has_value());
    REQUIRE(content->find("FROM gcc:14") != std::string::npos);

    REQUIRE(stub.calls().size() == 1);
    REQUIRE(stub.calls()[0] == "docker build -t demo:latest " +
                                   cup::cmd::docker_image_dir(proj, "demo").string());
}

TEST_CASE("resolve_verify_image fails when the default image does not build", "[cmd][compiler]") {
    const TempDir dir;
    const DockerConfig docker{
        .images = {DockerImage{.name = "demo", .base = "gcc:14", .is_default = true}}};
    const Project proj = make_project(dir, 11, 16, docker);
    StubRunCommand stub(/*fail=*/true);

    REQUIRE_FALSE(cup::cmd::resolve_verify_image(proj, "").has_value());
}

TEST_CASE("verify_compiler runs the project inside the verify image", "[cmd][compiler]") {
    const TempDir dir;
    Project proj = make_project(dir, 11, 16);
    proj.config.compiler.verify_image = "cup-cxx:latest";
    StubRunCommand stub;

    REQUIRE(cup::cmd::verify_compiler(proj, {}).has_value());
    REQUIRE(stub.calls().size() == 1);
    const std::string mount = proj.root.string() + ":/work:ro";
    for (const std::string_view want :
        std::vector<std::string_view>{"docker run --rm", mount, "cup-cxx:latest", "cmake -S /work"}) {
        REQUIRE(stub.calls()[0].find(want) != std::string::npos);
    }
}

TEST_CASE("verify_compiler fails when the build fails", "[cmd][compiler]") {
    const TempDir dir;
    const Project proj = make_project(dir, 11, 16);
    StubRunCommand stub(/*fail=*/true);
    REQUIRE_FALSE(cup::cmd::verify_compiler(proj, std::vector<std::string>{"--image", "cxx:15"}).has_value());
}

TEST_CASE("verify_compiler rejects a positional argument and a missing image", "[cmd][compiler]") {
    const TempDir dir;
    const Project proj = make_project(dir, 11, 16);
    REQUIRE_FALSE(cup::cmd::verify_compiler(proj, {}).has_value());
    REQUIRE_FALSE(cup::cmd::verify_compiler(proj, std::vector<std::string>{"extra"}).has_value());
}

TEST_CASE("set_compiler (no-verify) rewrites cup.toml and the CMake guard", "[cmd][compiler]") {
    const TempDir dir;
    const Project proj = make_project(dir, 11, 16);

    REQUIRE(cup::cmd::set_compiler(proj, std::vector<std::string>{"gcc", "12", "--no-verify"})
                .has_value());

    const auto toml = read_whole_file(dir.path() / "cup.toml");
    REQUIRE(toml.has_value());
    REQUIRE(toml->find("gcc = 12") != std::string::npos);
    REQUIRE(toml->find("clang = 16") != std::string::npos);

    const auto cmake = read_whole_file(dir.path() / "CMakeLists.txt");
    REQUIRE(cmake.has_value());
    REQUIRE(cmake->find("VERSION_LESS 12") != std::string::npos);
}

TEST_CASE("set_compiler refuses before touching files when it cannot verify",
          "[cmd][compiler]") {
    const TempDir dir;
    const Project proj = make_project(dir, 11, 16);

    REQUIRE_FALSE(cup::cmd::set_compiler(proj, std::vector<std::string>{"gcc", "12"}).has_value());

    const auto toml = read_whole_file(dir.path() / "cup.toml");
    REQUIRE(toml.has_value());
    REQUIRE(toml->find("gcc = 12") == std::string::npos);
}

TEST_CASE("set_compiler rejects bad arguments", "[cmd][compiler]") {
    const TempDir dir;
    const Project proj = make_project(dir, 11, 16);
    REQUIRE_FALSE(
        cup::cmd::set_compiler(proj, std::vector<std::string>{"rustc", "1", "--no-verify"}).has_value());
    REQUIRE_FALSE(cup::cmd::set_compiler(proj, std::vector<std::string>{"gcc", "notanumber", "--no-verify"})
                      .has_value());
    REQUIRE_FALSE(
        cup::cmd::set_compiler(proj, std::vector<std::string>{"gcc", "--no-verify"}).has_value());
}

TEST_CASE("set_compiler restores cup.toml when the verification build fails",
          "[cmd][compiler]") {
    const TempDir dir;
    const Project proj = make_project(dir, 11, 16);
    const auto before = read_whole_file(dir.path() / "cup.toml");
    StubRunCommand stub(/*fail=*/true);

    REQUIRE_FALSE(
        cup::cmd::set_compiler(proj, std::vector<std::string>{"gcc", "12", "--image", "cxx:15"})
            .has_value());

    REQUIRE(read_whole_file(dir.path() / "cup.toml") == before);
}

TEST_CASE("set_compiler onto a marker-less CMakeLists restores cup.toml", "[cmd][compiler]") {
    const TempDir dir;
    Config cfg{.name = "demo", .cpp_standard = 20, .build_tool = "cmake"};
    REQUIRE(cup::project::write_config(dir.path(), cfg).has_value());
    dir.write("CMakeLists.txt", "project(demo VERSION 0.1.0 LANGUAGES C CXX)\n");
    const Project proj{dir.path(), cfg};
    const auto before = read_whole_file(dir.path() / "cup.toml");

    REQUIRE_FALSE(
        cup::cmd::set_compiler(proj, std::vector<std::string>{"gcc", "12", "--no-verify"}).has_value());
    REQUIRE(read_whole_file(dir.path() / "cup.toml") == before);
}

TEST_CASE("set_compiler (no-verify) rewrites cup.toml on a Make project", "[cmd][compiler][make]") {
    const TempDir dir;
    const Project proj = make_project_make(dir);

    REQUIRE(cup::cmd::set_compiler(proj, std::vector<std::string>{"gcc", "12", "--no-verify"})
                .has_value());

    const auto toml = read_whole_file(dir.path() / "cup.toml");
    REQUIRE(toml.has_value());
    REQUIRE(toml->find("gcc = 12") != std::string::npos);
}

TEST_CASE("run_compiler dispatches show/bogus and resolves the project from cwd",
          "[cmd][compiler]") {
    const TempDir dir;
    make_project(dir, 11, 16);
    const ScopedCwd cwd(dir.path());

    REQUIRE(cup::cmd::run_compiler({}).has_value());
    REQUIRE(cup::cmd::run_compiler(std::vector<std::string>{"show"}).has_value());
    REQUIRE_FALSE(cup::cmd::run_compiler(std::vector<std::string>{"bogus"}).has_value());
}

TEST_CASE("run_compiler outside a project reports project::find's error", "[cmd][compiler]") {
    const TempDir dir;
    const ScopedCwd cwd(dir.path());
    REQUIRE_FALSE(cup::cmd::run_compiler({}).has_value());
}
