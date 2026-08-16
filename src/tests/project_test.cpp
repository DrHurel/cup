
#include <catch2/catch_test_macros.hpp>

#include <unistd.h>

#include <expected>
#include <filesystem>
#include <format>
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

using cup::project::CompilerConfig;
using cup::project::Config;
using cup::project::DockerConfig;
using cup::project::DockerImage;
using cup::test::TempDir;

[[nodiscard]] std::string slurp(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

}

TEST_CASE("standard defaults to 23 when unset", "[project][config]") {
    REQUIRE(Config{}.standard() == 23);
    REQUIRE(Config{.cpp_standard = 17}.standard() == 17);
}

TEST_CASE("has_floor sees a pinned compiler, not verify_image", "[project][compiler]") {
    REQUIRE_FALSE(cup::project::CompilerConfig{}.has_floor());
    REQUIRE(cup::project::make_compiler_config(15, 0).has_floor());
    REQUIRE(cup::project::make_compiler_config(0, 17).has_floor());
    REQUIRE_FALSE(cup::project::CompilerConfig{.verify_image = "cxx:15"}.has_floor());
}

TEST_CASE("make_compiler_config leaves a 0 version unpinned", "[project][compiler]") {
    const auto cc = cup::project::make_compiler_config(15, 0);
    REQUIRE(cc.gcc_floor() == 15);
    REQUIRE_FALSE(cc.clang.has_value());
    REQUIRE(cc.clang_floor() == 0);
}

TEST_CASE("uses_modules is true from C++20 up", "[project][modules]") {
    const std::vector<std::pair<int, bool>> cases{
        {0, true},
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

TEST_CASE("uses_std_module is opt-in via std_module, regardless of standard",
          "[project][stdmodule]") {
    SECTION("unset: always false -- `import std;` sits behind CMake's shifting "
            "experimental gate, so it is never on by default, C++23 included") {
        const std::vector<std::pair<int, bool>> cases{
            {0, false},
            {23, false},
            {20, false},
            {17, false},
        };
        for (const auto& [std_version, want] : cases) {
            INFO("cpp_standard = " << std_version);
            REQUIRE(Config{.cpp_standard = std_version}.uses_std_module() == want);
        }
    }

    SECTION("std_module overrides in both directions") {
        REQUIRE(Config{.cpp_standard = 23, .std_module = true}.uses_std_module());
        REQUIRE_FALSE(Config{.cpp_standard = 26, .std_module = false}.uses_std_module());
    }
}

TEST_CASE("an explicit std_module = false survives a rewrite", "[project][stdmodule][roundtrip]") {
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

TEST_CASE("tool defaults to cmake when unset", "[project][tool]") {
    REQUIRE(Config{}.tool() == cup::project::kToolCMake);
    REQUIRE(Config{.build_tool = std::string(cup::project::kToolMake)}.tool() ==
            cup::project::kToolMake);
}

TEST_CASE("uses_make is true only for the make build tool", "[project][tool]") {
    const std::vector<std::pair<std::string, bool>> cases{
        {"", false},
        {std::string(cup::project::kToolCMake), false},
        {std::string(cup::project::kToolMake), true},
    };
    for (const auto& [tool, want] : cases) {
        INFO("build_tool = " << tool);
        const cup::project::Project p{.root = "/proj", .config = Config{.build_tool = tool}};
        REQUIRE(p.uses_make() == want);
    }
}

TEST_CASE("src and path join onto the project root", "[project][paths]") {
    const cup::project::Project p{.root = "/proj", .config = {}};
    REQUIRE(p.src() == std::filesystem::path("/proj/src"));
    REQUIRE(p.path("a", "b") == std::filesystem::path("/proj/a/b"));
}

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

TEST_CASE("the [docker] table round-trips and its lookups work", "[project][docker]") {
    const TempDir root;
    const Config cfg{
        .name = "demo",
        .cpp_standard = 20,
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

    DockerConfig mutable_docker = found->config.docker;
    REQUIRE(mutable_docker.find("runtime") != nullptr);
    REQUIRE(mutable_docker.find("ghost") == nullptr);
    mutable_docker.find("runtime")->version = 7;
    REQUIRE(mutable_docker.find("runtime")->version == 7);
    REQUIRE(mutable_docker.default_image() != nullptr);
    REQUIRE(mutable_docker.default_image()->name == "demo");

    REQUIRE(DockerConfig{}.default_image() == nullptr);
}

TEST_CASE("config equality accounts for every field", "[project][config][equality]") {
    const Config base{
        .name = "demo",
        .cup_version = "0.1.0",
        .cpp_standard = 23,
        .std_module = false,
        .build_tool = "cmake",
        .compiler = CompilerConfig{.gcc = 14, .clang = 18, .verify_image = "gcc:14"},
        .docker = DockerConfig{.registry = "docker.io/youruser",
                               .images = {DockerImage{.name = "demo",
                                                      .base = "gcc:14",
                                                      .version = 3,
                                                      .hash = "abc",
                                                      .is_default = true}}}};

    REQUIRE(base == Config(base));

    const auto changed = [&base](auto&& mutate) {
        Config other = base;
        mutate(other);
        return other;
    };
    const std::vector<std::pair<const char*, Config>> cases{
        {"name", changed([](Config& c) { c.name = "other"; })},
        {"cup_version", changed([](Config& c) { c.cup_version = "0.2.0"; })},
        {"cpp_standard", changed([](Config& c) { c.cpp_standard = 20; })},
        {"std_module", changed([](Config& c) { c.std_module = true; })},
        {"build_tool", changed([](Config& c) { c.build_tool = "make"; })},
        {"compiler.gcc", changed([](Config& c) { c.compiler.gcc = 15; })},
        {"compiler.clang", changed([](Config& c) { c.compiler.clang = 19; })},
        {"compiler.verify_image", changed([](Config& c) { c.compiler.verify_image = "gcc:15"; })},
        {"docker.registry", changed([](Config& c) { c.docker.registry = "ghcr.io/youruser"; })},
        {"docker.images", changed([](Config& c) { c.docker.images.clear(); })},
        {"image.name", changed([](Config& c) { c.docker.images[0].name = "other"; })},
        {"image.base", changed([](Config& c) { c.docker.images[0].base = "gcc:15"; })},
        {"image.version", changed([](Config& c) { c.docker.images[0].version = 4; })},
        {"image.hash", changed([](Config& c) { c.docker.images[0].hash = "def"; })},
        {"image.default", changed([](Config& c) { c.docker.images[0].is_default = false; })},
    };

    for (const auto& [field, other] : cases) {
        INFO("differs in " << field);
        REQUIRE_FALSE(base == other);
        REQUIRE_FALSE(other == base);
    }
}

TEST_CASE("empty reports the tables cup.toml leaves out", "[project][config]") {
    REQUIRE(CompilerConfig{}.empty());
    REQUIRE_FALSE(cup::project::make_compiler_config(14, 0).empty());
    REQUIRE_FALSE(cup::project::make_compiler_config(0, 18).empty());
    REQUIRE_FALSE(CompilerConfig{.verify_image = "gcc:14-bookworm"}.empty());

    REQUIRE(DockerConfig{}.empty());
    REQUIRE_FALSE(DockerConfig{.registry = "docker.io/youruser"}.empty());
    REQUIRE_FALSE(DockerConfig{.images = {DockerImage{.name = "demo"}}}.empty());
}

TEST_CASE("default_image reports none when no image is marked default", "[project][docker]") {
    const DockerConfig docker{
        .images = {DockerImage{.name = "one"}, DockerImage{.name = "two"}}};

    REQUIRE(docker.default_image() == nullptr);
    REQUIRE(docker.find("one") != nullptr);
    REQUIRE(docker.find("two")->name == "two");

    DockerConfig mutable_docker = docker;
    REQUIRE(mutable_docker.default_image() == nullptr);
    REQUIRE(mutable_docker.find("two") != nullptr);
}

TEST_CASE("find_from walks up to the project root", "[project][find]") {
    const TempDir root;
    REQUIRE(
        cup::project::write_config(root, Config{.name = "demo", .cpp_standard = 20}).has_value());
    const std::filesystem::path nested = root.path() / "src" / "libs" / "utils";
    std::filesystem::create_directories(nested);

    const auto found = cup::project::find_from(nested);
    REQUIRE(found.has_value());
    REQUIRE(found->root == root.path());
}

TEST_CASE("find_from errors outside any project", "[project][find]") {
    const TempDir dir;
    const auto found = cup::project::find_from(dir);
    REQUIRE_FALSE(found.has_value());
    REQUIRE(found.error().message().starts_with("not inside a cup project"));
}

TEST_CASE("find_from stops on a relative start with no parent", "[project][find]") {
    const auto found = cup::project::find_from("no-such-directory-relative-to-cwd");
    REQUIRE_FALSE(found.has_value());
    REQUIRE(found.error().message().starts_with("not inside a cup project"));
}

TEST_CASE("find_from errors on a malformed cup.toml", "[project][find]") {
    const TempDir root;
    root.write(cup::project::kMarker, "name = \"unterminated");

    const auto found = cup::project::find_from(root);
    REQUIRE_FALSE(found.has_value());
    REQUIRE(found.error().message().starts_with("reading "));
}

TEST_CASE("find_from reports a cup.toml it cannot read", "[project][find]") {
    if (::geteuid() == 0) {
        SKIP("root ignores the permission bits, so the file stays readable");
    }
    const TempDir root;
    root.write(cup::project::kMarker, "name = \"demo\"\n");
    const std::filesystem::path marker = root.path() / cup::project::kMarker;
    std::filesystem::permissions(marker, std::filesystem::perms::none);

    const auto found = cup::project::find_from(root);
    REQUIRE_FALSE(found.has_value());
    REQUIRE(found.error().message() == std::format("reading {}", marker.string()));
}

TEST_CASE("find starts from the working directory", "[project][find]") {
    const auto from_cwd = cup::project::find_from(std::filesystem::current_path());
    const auto found = cup::project::find();

    REQUIRE(found.has_value() == from_cwd.has_value());
    if (found.has_value()) {
        REQUIRE(found->root == from_cwd->root);
        REQUIRE(found->config == from_cwd->config);
    } else {
        REQUIRE(found.error().message() == from_cwd.error().message());
    }
}

TEST_CASE("write_config reports a root it cannot write", "[project][write]") {
    const TempDir tmp;
    const std::filesystem::path missing = tmp.path() / "no-such-directory";

    const auto wrote = cup::project::write_config(missing, Config{.name = "demo"});
    REQUIRE_FALSE(wrote.has_value());
    REQUIRE(wrote.error().message() ==
            std::format("writing {}", (missing / cup::project::kMarker).string()));
}

TEST_CASE("parse_config reports a field of the wrong type", "[project][toml]") {
    const struct Case {
        const char* text;
        const char* field;
    } cases[]{
        {"name = 1", "name"},
        {"cup_version = 1", "cup_version"},
        {R"(cpp_standard = "23")", "cpp_standard"},
        {"cpp_standard = 20\nstd_module = \"false\"", "std_module"},
        {"cpp_standard = 20\nbuild_tool = 1", "build_tool"},
        {"cpp_standard = 20\n[compiler]\ngcc = \"14\"", "gcc"},
        {"cpp_standard = 20\n[compiler]\nclang = \"18\"", "clang"},
        {"cpp_standard = 20\n[compiler]\nverify_image = 1", "verify_image"},
        {"cpp_standard = 20\n[docker]\nregistry = 1", "registry"},
        {"cpp_standard = 20\n[[docker.image]]\nname = 1", "name"},
        {"cpp_standard = 20\n[[docker.image]]\nbase = 1", "base"},
        {"cpp_standard = 20\n[[docker.image]]\nversion = \"3\"", "version"},
        {"cpp_standard = 20\n[[docker.image]]\nhash = 1", "hash"},
        {"cpp_standard = 20\n[[docker.image]]\ndefault = \"yes\"", "default"},
    };

    for (const auto& c : cases) {
        INFO(c.text);
        const auto cfg = cup::project::parse_config(c.text);
        REQUIRE_FALSE(cfg.has_value());
        REQUIRE(cfg.error().message() ==
                std::format("field {} has the wrong type", c.field));
    }
}

TEST_CASE("parse_config rejects a docker.image entry that is not a table", "[project][toml]") {
    const auto cfg = cup::project::parse_config("cpp_standard = 20\n[docker]\nimage = [1]\n");
    REQUIRE_FALSE(cfg.has_value());
    REQUIRE(cfg.error().message() == "docker.image entries must be tables");
}

TEST_CASE("parse_config accepts a [docker] table with no images", "[project][toml]") {
    const auto cfg = cup::project::parse_config(
        "cpp_standard = 20\n[docker]\nregistry = \"docker.io/youruser\"\n");
    REQUIRE(cfg.has_value());
    REQUIRE(cfg->docker.registry == "docker.io/youruser");
    REQUIRE(cfg->docker.images.empty());
    REQUIRE_FALSE(cfg->docker.empty());
}

TEST_CASE("parse_config ignores keys it does not know", "[project][toml]") {
    const auto cfg =
        cup::project::parse_config("name = \"demo\"\ncpp_standard = 20\nfuture_thing = 1\n");
    REQUIRE(cfg.has_value());
    REQUIRE(cfg->name == "demo");
}

TEST_CASE("parse_config rejects an out-of-range cpp_standard", "[project][toml]") {
    const auto cfg = cup::project::parse_config("name = \"demo\"\ncpp_standard = 99\n");
    REQUIRE_FALSE(cfg.has_value());
    REQUIRE(cfg.error().message().find("cpp_standard") != std::string::npos);
    REQUIRE(cfg.error().message().find("99") != std::string::npos);
}

TEST_CASE("parse_config rejects an omitted cpp_standard", "[project][toml]") {
    const auto cfg = cup::project::parse_config("name = \"demo\"\n");
    REQUIRE_FALSE(cfg.has_value());
    REQUIRE(cfg.error().message().find("cpp_standard") != std::string::npos);
    REQUIRE(cfg.error().message().find("0") != std::string::npos);
}

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

TEST_CASE("to_toml writes verify_image after the version floors", "[project][toml][parity]") {
    cup::project::CompilerConfig compiler = cup::project::make_compiler_config(14, 0);
    compiler.verify_image = "gcc:14-bookworm";
    const Config cfg{.name = "demo", .compiler = compiler};
    REQUIRE(cup::project::to_toml(cfg) == R"(name = "demo"
cpp_standard = 0

[compiler]
  gcc = 14
  verify_image = "gcc:14-bookworm"
)");
}

TEST_CASE("to_toml omits an unpinned gcc", "[project][toml][parity]") {
    const Config cfg{.name = "demo", .compiler = cup::project::make_compiler_config(0, 18)};
    REQUIRE(cup::project::to_toml(cfg) == R"(name = "demo"
cpp_standard = 0

[compiler]
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

TEST_CASE("to_toml escapes control characters like the Go encoder", "[project][toml][parity]") {
    const Config cfg{.name = "\b\t\n\f\r\x{01}\x{7f}"};
    REQUIRE(cup::project::to_toml(cfg) == R"(name = "\b\t\n\f\r\u0001\u007f")"
                                          "\ncpp_standard = 0\n");
}

TEST_CASE("cup's own cup.toml survives a decode/encode round trip",
          "[project][toml][parity][dogfood]") {
    const std::filesystem::path marker =
        std::filesystem::path(CUP_PROJECT_ROOT) / cup::project::kMarker;
    const std::string original = slurp(marker);
    REQUIRE_FALSE(original.empty());

    const auto cfg = cup::project::parse_config(original);
    REQUIRE(cfg.has_value());
    REQUIRE(cup::project::to_toml(*cfg) == original);

    REQUIRE(cfg->standard() == 23);
    REQUIRE_FALSE(cfg->uses_std_module());
}
