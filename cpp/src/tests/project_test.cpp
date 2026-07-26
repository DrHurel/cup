// Port of internal/project/project_test.go.
//
// The Go tests already pass against the Go implementation, so they define the
// behaviour precisely; each TEST_CASE below names the Go test it replaces. The
// byte-parity cases at the bottom have no Go counterpart — they pin cup.toml's
// exact serialisation, which the Phase 5 cross-validation harness depends on.

#include <catch2/catch_test_macros.hpp>

// cup.project returns std::expected<void, Error> from write_config, but a module
// re-exports no declaration from its global module fragment — the void
// specialisation has to be visible here, so this consumer includes <expected>
// itself. Same rule as the <functional> note in ui_test.cpp.
#include <expected>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "TempDir.hpp"

import cup.project;

namespace {

using cup::project::Config;
using cup::project::DockerConfig;
using cup::project::DockerImage;
using cup::test::TempDir;

[[nodiscard]] std::string slurp(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

}  // namespace

// Go: TestConfigStandard
TEST_CASE("standard defaults to 23 when unset", "[project][config]") {
    REQUIRE(Config{}.standard() == 23);
    REQUIRE(Config{.cpp_standard = 17}.standard() == 17);
}

// Go: TestCompilerHasFloor
TEST_CASE("has_floor sees a pinned compiler, not verify_image", "[project][compiler]") {
    REQUIRE_FALSE(cup::project::CompilerConfig{}.has_floor());
    REQUIRE(cup::project::make_compiler_config(15, 0).has_floor());
    REQUIRE(cup::project::make_compiler_config(0, 17).has_floor());
    // verify_image alone is not a version floor.
    REQUIRE_FALSE(cup::project::CompilerConfig{.verify_image = "cxx:15"}.has_floor());
}

// Go: TestNewCompilerConfig
TEST_CASE("make_compiler_config leaves a 0 version unpinned", "[project][compiler]") {
    const auto cc = cup::project::make_compiler_config(15, 0);
    REQUIRE(cc.gcc_floor() == 15);
    REQUIRE_FALSE(cc.clang.has_value());
    REQUIRE(cc.clang_floor() == 0);
}

// Go: TestUsesModules
TEST_CASE("uses_modules is true from C++20 up", "[project][modules]") {
    const std::vector<std::pair<int, bool>> cases{
        {0, true},  // default 23
        {23, true},
        {20, true},
        {17, false},
        {11, false},
    };
    for (const auto& [std_version, want] : cases) {
        INFO("cpp_standard = " << std_version);
        const cup::project::Project p{.root = "/proj", .config = Config{.cpp_standard = std_version}};
        REQUIRE(p.uses_modules() == want);
    }
}

// Go: TestUsesStdModule
TEST_CASE("uses_std_module follows the standard until std_module overrides it",
          "[project][stdmodule]") {
    SECTION("unset: the standard decides, and C++23 is the first with a std module") {
        const std::vector<std::pair<int, bool>> cases{
            {0, true},  // default 23
            {23, true},
            {20, false},
            {17, false},
        };
        for (const auto& [std_version, want] : cases) {
            INFO("cpp_standard = " << std_version);
            REQUIRE(Config{.cpp_standard = std_version}.uses_std_module() == want);
        }
    }

    SECTION("std_module overrides in both directions") {
        // `false` on C++23 is the interesting one: named modules on a GCC 14
        // floor, no `import std;` — cup's own C++ port.
        REQUIRE_FALSE(Config{.cpp_standard = 23, .std_module = false}.uses_std_module());
        REQUIRE(Config{.cpp_standard = 26, .std_module = true}.uses_std_module());
    }
}

// Go: TestUsesStdModuleRoundTrip
TEST_CASE("an explicit std_module = false survives a rewrite", "[project][stdmodule][roundtrip]") {
    // `cup compiler set` rewrites cup.toml through write_config, so an explicit
    // std_module = false must survive the round trip rather than being dropped as
    // an empty value — dropping it would silently flip the project onto
    // `import std;`.
    const TempDir dir;
    REQUIRE(cup::project::write_config(
                dir, Config{.name = "cup", .cpp_standard = 23, .std_module = false})
                .has_value());

    const auto found = cup::project::find_from(dir);
    REQUIRE(found.has_value());
    REQUIRE(found->config.std_module.has_value());
    REQUIRE_FALSE(*found->config.std_module);
    REQUIRE_FALSE(found->config.uses_std_module());
}

// Go: TestConfigTool
TEST_CASE("tool defaults to cmake when unset", "[project][tool]") {
    REQUIRE(Config{}.tool() == cup::project::kToolCMake);
    REQUIRE(Config{.build_tool = std::string(cup::project::kToolMake)}.tool() ==
            cup::project::kToolMake);
}

// Go: TestUsesMake
TEST_CASE("uses_make is true only for the make build tool", "[project][tool]") {
    const std::vector<std::pair<std::string, bool>> cases{
        {"", false},  // unset -> CMake
        {std::string(cup::project::kToolCMake), false},
        {std::string(cup::project::kToolMake), true},
    };
    for (const auto& [tool, want] : cases) {
        INFO("build_tool = " << tool);
        const cup::project::Project p{.root = "/proj", .config = Config{.build_tool = tool}};
        REQUIRE(p.uses_make() == want);
    }
}

// Go: TestSrcAndPath
TEST_CASE("src and path join onto the project root", "[project][paths]") {
    const cup::project::Project p{.root = "/proj", .config = {}};
    REQUIRE(p.src() == std::filesystem::path("/proj/src"));
    REQUIRE(p.path("a", "b") == std::filesystem::path("/proj/a/b"));
}

// Go: TestWriteConfigRoundTrip
TEST_CASE("write_config round-trips through find_from", "[project][roundtrip]") {
    const TempDir root;
    const Config cfg{.name = "demo", .cup_version = "0.1.0", .cpp_standard = 20};
    REQUIRE(cup::project::write_config(root, cfg).has_value());
    REQUIRE(std::filesystem::exists(root.path() / cup::project::kMarker));

    const auto found = cup::project::find_from(root);
    REQUIRE(found.has_value());
    REQUIRE(found->config == cfg);
    REQUIRE(found->root == root.path());
}

// Go: TestDockerConfigRoundTrip
TEST_CASE("the [docker] table round-trips and its lookups work", "[project][docker]") {
    const TempDir root;
    const Config cfg{
        .name = "demo",
        .docker = DockerConfig{
            .registry = "docker.io/youruser",
            .images = {DockerImage{.name = "demo",
                                   .base = "gcc:14",
                                   .version = 3,
                                   .hash = "abc",
                                   .is_default = true},
                       DockerImage{.name = "runtime", .base = "debian:trixie-slim"}},
        }};
    REQUIRE(cup::project::write_config(root, cfg).has_value());

    const auto found = cup::project::find_from(root);
    REQUIRE(found.has_value());
    REQUIRE(found->config.docker == cfg.docker);

    const auto& docker = found->config.docker;
    REQUIRE(docker.default_image() != nullptr);
    REQUIRE(docker.default_image()->name == "demo");
    REQUIRE(docker.find("runtime") != nullptr);
    REQUIRE(docker.find("ghost") == nullptr);

    // A config with no default image reports none.
    REQUIRE(DockerConfig{}.default_image() == nullptr);
}

// Go: TestFindWalksUp
TEST_CASE("find_from walks up to the project root", "[project][find]") {
    const TempDir root;
    REQUIRE(cup::project::write_config(root, Config{.name = "demo"}).has_value());
    const std::filesystem::path nested = root.path() / "src" / "libs" / "utils";
    std::filesystem::create_directories(nested);

    const auto found = cup::project::find_from(nested);
    REQUIRE(found.has_value());
    REQUIRE(found->root == root.path());
}

// Go: TestFindNoProject
TEST_CASE("find_from errors outside any project", "[project][find]") {
    const TempDir dir;
    const auto found = cup::project::find_from(dir);
    REQUIRE_FALSE(found.has_value());
    REQUIRE(found.error().message().starts_with("not inside a cup project"));
}

// Go: TestFindInvalidToml
TEST_CASE("find_from errors on a malformed cup.toml", "[project][find]") {
    const TempDir root;
    root.write(cup::project::kMarker, "name = \"unterminated");

    const auto found = cup::project::find_from(root);
    REQUIRE_FALSE(found.has_value());
    REQUIRE(found.error().message().starts_with("reading "));
}

// --- Byte parity with the Go encoder -------------------------------------------
//
// No Go counterpart: these pin the exact bytes BurntSushi/toml produces for a
// Config, captured from the Go implementation. Phase 5 hands over only once both
// binaries write identical trees, and cup.toml is in every one of them — so a
// formatting drift here is a handover blocker, not a cosmetic issue.

// The rule that is easiest to get wrong: BurntSushi's omitempty covers empty
// strings, false bools and empty tables, but *not* numeric zero.
TEST_CASE("to_toml writes zero ints and omits empty strings", "[project][toml][parity]") {
    REQUIRE(cup::project::to_toml(Config{}) == "name = \"\"\ncpp_standard = 0\n");
    REQUIRE(cup::project::to_toml(Config{.name = "demo"}) ==
            "name = \"demo\"\ncpp_standard = 0\n");
}

TEST_CASE("to_toml matches the Go encoder for a full config", "[project][toml][parity]") {
    const Config cfg{.name = "cup",
                     .cup_version = "0.1.0",
                     .cpp_standard = 23,
                     .std_module = false,
                     .build_tool = "cmake",
                     .compiler = cup::project::make_compiler_config(14, 18)};
    REQUIRE(cup::project::to_toml(cfg) == R"(name = "cup"
cup_version = "0.1.0"
cpp_standard = 23
std_module = false
build_tool = "cmake"

[compiler]
  gcc = 14
  clang = 18
)");
}

TEST_CASE("to_toml matches the Go encoder for the [docker] table", "[project][toml][parity]") {
    const Config cfg{
        .name = "demo",
        .docker = DockerConfig{
            .registry = "docker.io/youruser",
            .images = {DockerImage{.name = "demo",
                                   .base = "gcc:14",
                                   .version = 3,
                                   .hash = "abc",
                                   .is_default = true},
                       DockerImage{.name = "runtime", .base = "debian:trixie-slim"}},
        }};
    // Note version = 0 on the runtime image, and the blank line before every
    // sub-table header.
    REQUIRE(cup::project::to_toml(cfg) == R"(name = "demo"
cpp_standard = 0

[docker]
  registry = "docker.io/youruser"

  [[docker.image]]
    name = "demo"
    base = "gcc:14"
    version = 3
    hash = "abc"
    default = true

  [[docker.image]]
    name = "runtime"
    base = "debian:trixie-slim"
    version = 0
)");
}

TEST_CASE("to_toml escapes basic strings like the Go encoder", "[project][toml][parity]") {
    const Config cfg{.name = R"(we"ird\name)", .build_tool = "make"};
    REQUIRE(cup::project::to_toml(cfg) == R"(name = "we\"ird\\name"
cpp_standard = 0
build_tool = "make"
)");
}

// The strongest parity check available: cup's own cup.toml was written by the Go
// cup, so re-emitting it from the C++ decoder must reproduce it byte for byte.
TEST_CASE("cup's own cup.toml survives a decode/encode round trip",
          "[project][toml][parity][dogfood]") {
    const std::filesystem::path marker =
        std::filesystem::path(CUP_PROJECT_ROOT) / cup::project::kMarker;
    const std::string original = slurp(marker);
    REQUIRE_FALSE(original.empty());

    const auto cfg = cup::project::parse_config(original);
    REQUIRE(cfg.has_value());
    REQUIRE(cup::project::to_toml(*cfg) == original);

    // And the decision that file exists to record.
    REQUIRE(cfg->standard() == 23);
    REQUIRE_FALSE(cfg->uses_std_module());
}
