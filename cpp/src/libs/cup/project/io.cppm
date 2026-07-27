module;
// Declarations only — the definitions live in Toml.cpp, a module implementation
// unit, and toml++ is included there rather than here.
//
// That split is forced. With `#include <toml++/toml.hpp>` in this partition's
// global module fragment, GCC 14 ICEs while the primary interface unit merges the
// partition's BMI:
//
//     In destructor 'toml::v3::impl::utf8_reader_interface::~utf8_reader_interface()':
//     internal compiler error: Segmentation fault   (maybe_clone_body)
//
// A module implementation unit's global module fragment never reaches any BMI, so
// moving the parser there sidesteps the bug entirely — and it is the same call
// GenerateEmbeddedTemplates.cmake already makes for the template corpus, for the
// same reason. The header budget here stays small enough to coexist with :config.
#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
export module cup.project:io;

import :config;
// Re-exported: utils::error::Error is the E of every result below.
export import utils.error;

export namespace cup::project {

// Project is a located cup project.
//
// It lives in :io rather than alongside Config because it is the one part of the
// model made of std::filesystem::path, and <filesystem> has to stay out of
// :config — see the header note there. That is no loss: a located project is an
// I/O concept, and the predicates below just forward to the pure ones on Config so
// call sites keep the Go shape (p.uses_modules(), not p.config.uses_modules()).
struct Project {
    std::filesystem::path root;
    Config config;

    // src returns the project's src/ directory.
    [[nodiscard]] std::filesystem::path src() const { return root / "src"; }

    // uses_modules reports whether the project's standard supports C++ modules
    // (C++20 and later); below that, cup scaffolds classic headers.
    [[nodiscard]] bool uses_modules() const { return config.uses_modules(); }

    // uses_make reports whether the project builds with Make rather than CMake.
    [[nodiscard]] bool uses_make() const { return config.uses_make(); }

    // path joins parts onto the project root. (Go: Path(parts ...string).)
    template <typename... Parts>
    [[nodiscard]] std::filesystem::path path(const Parts&... parts) const {
        std::filesystem::path joined = root;
        ((joined /= parts), ...);
        return joined;
    }
};

// to_toml renders cfg as cup.toml.
//
// Hand-written rather than delegated to toml++'s serialiser because the output has
// to match Go's BurntSushi/toml encoder byte for byte: cup rewrites cup.toml in
// place, and the Phase 5 cross-validation harness diffs trees produced by both
// binaries. Three of that encoder's rules are load-bearing and none of them is the
// obvious one:
//
//   - Zero *ints* are still written (`cpp_standard = 0`, `version = 0`), even
//     though the Go struct tags say omitempty — its notion of "empty" covers empty
//     strings, false bools, and empty tables, but not numeric zero.
//   - Empty strings, false bools, and wholly-empty sub-tables are omitted.
//   - A sub-table header is preceded by a blank line and its contents indented two
//     spaces per level, so `[[docker.image]]` sits at two and its keys at four.
[[nodiscard]] std::string to_toml(const Config& cfg);

// parse_config decodes cup.toml's text. Unknown keys are ignored, matching the Go
// decoder, so a cup.toml written by a newer cup still loads.
[[nodiscard]] std::expected<Config, utils::error::Error> parse_config(std::string_view text);

// write_config writes cup.toml at root.
[[nodiscard]] std::expected<void, utils::error::Error> write_config(
    const std::filesystem::path& root, const Config& cfg);

// find_from walks up from start looking for a cup.toml, returning the enclosing
// project. It errors if none is found.
//
// Go's Find reads the working directory itself; taking the starting directory as a
// parameter keeps the walk testable without chdir, which a parallel test runner
// cannot do safely. find() below is the Go signature.
[[nodiscard]] std::expected<Project, utils::error::Error> find_from(
    const std::filesystem::path& start);

// find locates the project enclosing the current working directory.
[[nodiscard]] std::expected<Project, utils::error::Error> find();

}  // namespace cup::project
