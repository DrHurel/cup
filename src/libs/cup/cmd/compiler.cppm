module;
#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
export module cup.cmd:compiler;

export import cup.error;
export import cup.project;

// Declarations only, defined in Compiler.cpp. Mirrors internal/cmd/compiler.go:
// `cup compiler` shows or changes the project's minimum compiler versions.
// Changing a floor is docker-verified — cup compiles the project in the
// configured toolchain image and reverts the change if the build fails — so a
// floor can never drift away from what the project actually compiles with.
export namespace cup::cmd {

// effective_compilers returns the project's minimum GCC and Clang major
// versions: cup.toml's [compiler] floors when set, else cup's per-standard
// defaults (scaffold::min_compilers).
[[nodiscard]] std::pair<int, int> effective_compilers(const project::Config& cfg);

// floor_label renders a minimum version for display; a zero means the
// compiler is not gated.
[[nodiscard]] std::string floor_label(int v);

// ParsedCompilerFlags is parse_compiler_flags' result: the --image and
// --no-verify options peeled out of a `cup compiler` invocation, plus
// whatever positional arguments remain.
struct ParsedCompilerFlags {
    std::string image;
    bool no_verify = false;
    std::vector<std::string> rest;
};

[[nodiscard]] std::expected<ParsedCompilerFlags, error::Error> parse_compiler_flags(
    std::span<const std::string> args);

// PlannedCompilerChange is plan_compiler_change's result: which compiler
// changed, its new version, and the resulting config (with the other
// compiler's *effective* floor materialised, so setting one never silently
// drops the other's default).
struct PlannedCompilerChange {
    std::string name;
    int version = 0;
    project::Config config;
};

[[nodiscard]] std::expected<PlannedCompilerChange, error::Error> plan_compiler_change(
    const project::Config& cur, std::span<const std::string> rest);

// has_verify_target reports whether a verify would have an image to run in,
// used to fail `cup compiler set` fast (before touching any files) when it
// cannot verify.
[[nodiscard]] bool has_verify_target(const project::Project& proj, std::string_view image);

[[nodiscard]] std::expected<void, error::Error> show_compilers(const project::Project& proj);

// resolve_verify_image picks the image `cup compiler verify` compiles in: an
// explicit override wins; otherwise the project's default build image is
// regenerated and rebuilt at :latest; failing that, a legacy verify_image is
// used as-is.
[[nodiscard]] std::expected<std::string, error::Error> resolve_verify_image(
    const project::Project& proj, std::string_view override_image);

// docker_verify compiles the project inside image to prove it still builds.
// The source tree is mounted read-only and the build runs in a
// container-local directory, so the check can neither mutate nor litter the
// project.
[[nodiscard]] std::expected<void, error::Error> docker_verify(const project::Project& proj,
                                                               std::string_view image);

// apply_compiler_floor writes cfg back to cup.toml and rewrites the root
// CMakeLists' compiler-guard block to match.
[[nodiscard]] std::expected<void, error::Error> apply_compiler_floor(
    const std::filesystem::path& root, const project::Config& cfg);

// commit_compiler_floor writes the new floor, then (unless no_verify)
// docker-compiles the project. Any failure restores cup.toml and the root
// CMakeLists byte-for-byte, so the change is all-or-nothing.
[[nodiscard]] std::expected<void, error::Error> commit_compiler_floor(
    const project::Project& proj, const project::Config& cfg, std::string_view image,
    bool no_verify);

[[nodiscard]] std::expected<void, error::Error> set_compiler(const project::Project& proj,
                                                              std::span<const std::string> args);

// verify_compiler compiles the project in the toolchain image without
// changing anything — a standalone "does it still build?" check.
[[nodiscard]] std::expected<void, error::Error> verify_compiler(
    const project::Project& proj, std::span<const std::string> args);

[[nodiscard]] std::expected<void, error::Error> run_compiler(std::span<const std::string> args);

}
