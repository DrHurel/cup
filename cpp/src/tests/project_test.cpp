// Port of internal/project/project_test.go.
//
// The Go tests already pass against the Go implementation, so they define the
// behaviour precisely; each TEST_CASE below names the Go test it replaces. The
// byte-parity cases at the bottom have no Go counterpart — they pin cup.toml's
// exact serialisation, which the Phase 5 cross-validation harness depends on.

#include <catch2/catch_test_macros.hpp>

#include <unistd.h>  // geteuid, for the permission-dependent read failure

// cup.project returns std::expected<void, Error> from write_config, but a module
// re-exports no declaration from its global module fragment — the void
// specialisation has to be visible here, so this consumer includes <expected>
// itself. Same rule as the <functional> note in ui_test.cpp.
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

    // The non-const overloads exist so `cup docker set` can bump a version in place;
    // they must hand back the same entries.
    DockerConfig mutable_docker = found->config.docker;
    REQUIRE(mutable_docker.find("runtime") != nullptr);
    REQUIRE(mutable_docker.find("ghost") == nullptr);
    mutable_docker.find("runtime")->version = 7;
    REQUIRE(mutable_docker.find("runtime")->version == 7);
    REQUIRE(mutable_docker.default_image() != nullptr);
    REQUIRE(mutable_docker.default_image()->name == "demo");

    // A config with no default image reports none.
    REQUIRE(DockerConfig{}.default_image() == nullptr);
}

// No Go counterpart: Go compared configs with reflect.DeepEqual, so there was no
// equality of cup's own to check. The port hand-writes one per struct — see the note
// on DockerImage::operator== for why they are not `= default` — and a hand-written
// comparison is exactly the kind that quietly loses a field. One that did would make
// a changed config compare equal to the one on disk, and `cup compiler set` would
// skip the rewrite it was asked for.
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

    REQUIRE(base == Config(base));  // every field equal: the whole chain runs

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

// empty() is what decides whether a sub-table is written to cup.toml at all, so a
// table that is not empty for a reason no test names would be dropped on the next
// rewrite — taking the setting it held with it.
TEST_CASE("empty reports the tables cup.toml leaves out", "[project][config]") {
    REQUIRE(CompilerConfig{}.empty());
    REQUIRE_FALSE(cup::project::make_compiler_config(14, 0).empty());
    REQUIRE_FALSE(cup::project::make_compiler_config(0, 18).empty());
    // A recorded verify_image is not a floor (has_floor says so), but it is still
    // content: the table has to be written for it to survive.
    REQUIRE_FALSE(CompilerConfig{.verify_image = "gcc:14-bookworm"}.empty());

    REQUIRE(DockerConfig{}.empty());
    REQUIRE_FALSE(DockerConfig{.registry = "docker.io/youruser"}.empty());
    REQUIRE_FALSE(DockerConfig{.images = {DockerImage{.name = "demo"}}}.empty());
}

// The lookups walk to the end of the list and report nothing rather than handing
// back the last image they looked at. Projects predating the [docker] table have no
// default at all, and `cup docker build` picks its image from this.
TEST_CASE("default_image reports none when no image is marked default", "[project][docker]") {
    const DockerConfig docker{
        .images = {DockerImage{.name = "one"}, DockerImage{.name = "two"}}};

    REQUIRE(docker.default_image() == nullptr);
    REQUIRE(docker.find("one") != nullptr);
    REQUIRE(docker.find("two")->name == "two");

    // The non-const overloads are a separate instantiation, and share the walk.
    DockerConfig mutable_docker = docker;
    REQUIRE(mutable_docker.default_image() == nullptr);
    REQUIRE(mutable_docker.find("two") != nullptr);
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

// The walk's other stopping condition. parent_path() of an absolute root is the root
// itself, which the case above reaches, but of a bare relative name it is empty — and
// a loop that only compared parent to dir would spin on "" for ever rather than
// report that there is no project.
TEST_CASE("find_from stops on a relative start with no parent", "[project][find]") {
    const auto found = cup::project::find_from("no-such-directory-relative-to-cwd");
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

// No Go counterpart in name, but the behaviour is Go's: Find surfaces os.ReadFile's
// error. A cup.toml that stat()s but cannot be opened has to be reported, not treated
// as absent — otherwise the walk carries on past it and finds a project higher up, or
// claims there is none.
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

// find() is find_from() rooted at the working directory, and that lookup is the only
// thing it adds — so the check is that the two agree, wherever ctest happens to run
// the suite from. Deliberately no chdir: the walk takes its start as a parameter
// precisely so the tests need none.
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

// No Go counterpart: `cup compiler set` and `cup docker add` both rewrite cup.toml
// through write_config, so a failed write has to be reported rather than leave the
// caller believing it landed.
TEST_CASE("write_config reports a root it cannot write", "[project][write]") {
    const TempDir tmp;
    const std::filesystem::path missing = tmp.path() / "no-such-directory";

    const auto wrote = cup::project::write_config(missing, Config{.name = "demo"});
    REQUIRE_FALSE(wrote.has_value());
    REQUIRE(wrote.error().message() ==
            std::format("writing {}", (missing / cup::project::kMarker).string()));
}

// No Go counterpart by name, but the rule is Go's: its decoder errors on a key of the
// wrong type instead of falling back to the zero value, so a typo in cup.toml is
// reported rather than silently changing the project — a quoted `cpp_standard = "23"`
// must not read as C++0.
TEST_CASE("parse_config reports a field of the wrong type", "[project][toml]") {
    const struct Case {
        const char* text;
        const char* field;
    } cases[]{
        {"name = 1", "name"},
        {"cup_version = 1", "cup_version"},
        {R"(cpp_standard = "23")", "cpp_standard"},
        {R"(std_module = "false")", "std_module"},
        {"build_tool = 1", "build_tool"},
        {"[compiler]\ngcc = \"14\"", "gcc"},
        {"[compiler]\nclang = \"18\"", "clang"},
        {"[compiler]\nverify_image = 1", "verify_image"},
        {"[docker]\nregistry = 1", "registry"},
        {"[[docker.image]]\nname = 1", "name"},
        {"[[docker.image]]\nbase = 1", "base"},
        {"[[docker.image]]\nversion = \"3\"", "version"},
        {"[[docker.image]]\nhash = 1", "hash"},
        {"[[docker.image]]\ndefault = \"yes\"", "default"},
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
    const auto cfg = cup::project::parse_config("[docker]\nimage = [1]\n");
    REQUIRE_FALSE(cfg.has_value());
    REQUIRE(cfg.error().message() == "docker.image entries must be tables");
}

// A [docker] table can carry a registry and no images at all — `cup docker push`
// pointed at a registry before any image was added. The array is optional, not a
// requirement of the table, so its absence leaves an empty list rather than failing
// the decode or abandoning the registry that was read just before it.
TEST_CASE("parse_config accepts a [docker] table with no images", "[project][toml]") {
    const auto cfg = cup::project::parse_config("[docker]\nregistry = \"docker.io/youruser\"\n");
    REQUIRE(cfg.has_value());
    REQUIRE(cfg->docker.registry == "docker.io/youruser");
    REQUIRE(cfg->docker.images.empty());
    REQUIRE_FALSE(cfg->docker.empty());  // and so it is still written back out
}

// Unknown keys are ignored, matching the Go decoder, so a cup.toml written by a newer
// cup still loads instead of failing the whole run.
TEST_CASE("parse_config ignores keys it does not know", "[project][toml]") {
    const auto cfg = cup::project::parse_config("name = \"demo\"\nfuture_thing = 1\n");
    REQUIRE(cfg.has_value());
    REQUIRE(cfg->name == "demo");
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

// verify_image is [compiler]'s third key and its only string: `cup compiler set`
// records the image it verified a floor change in, so a later rewrite has to keep it —
// and keep it after the floors, which is the order the Go struct declares.
TEST_CASE("to_toml writes verify_image after the version floors", "[project][toml][parity]") {
    cup::project::CompilerConfig compiler = cup::project::make_compiler_config(14, 0);
    compiler.verify_image = "gcc:14-bookworm";
    const Config cfg{.name = "demo", .compiler = compiler};
    // Note the absent clang line: an unpinned compiler is left out of the table.
    REQUIRE(cup::project::to_toml(cfg) == R"(name = "demo"
cpp_standard = 0

[compiler]
  gcc = 14
  verify_image = "gcc:14-bookworm"
)");
}

// The mirror of that case: pinned clang, unpinned gcc. The omitted key is the first
// one in the table this time, so the [compiler] header still has to be written and
// the surviving floor still has to follow it.
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

// The rest of BurntSushi's escape table: the five control characters it names, then
// the \u00XX form it falls back to. A project name never contains these, but a
// template or an image tag pulled from a registry can, and an unescaped control byte
// writes a cup.toml that cup itself cannot parse back.
TEST_CASE("to_toml escapes control characters like the Go encoder", "[project][toml][parity]") {
    // 0x01 stands for the C0 range; 0x7f is DEL, which sits above it and is the one
    // easy to leave out. Both render lowercase, as Go's encoder writes them.
    const Config cfg{.name = "\b\t\n\f\r\x{01}\x{7f}"};
    REQUIRE(cup::project::to_toml(cfg) == R"(name = "\b\t\n\f\r\u0001\u007f")"
                                          "\ncpp_standard = 0\n");
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
