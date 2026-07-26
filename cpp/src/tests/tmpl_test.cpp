// Port of internal/tmpl/tmpl_test.go.
//
// The Go tests already pass against the Go implementation, so they define the
// behaviour precisely; each TEST_CASE below names the Go test it replaces.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
// copy_builtin returns std::expected<void, Error>, whose void specialisation a
// module cannot re-export from its global module fragment — see the note in
// ui_test.cpp.
#include <expected>
#include <filesystem>
#include <string>
#include <vector>

#include "TempDir.hpp"

import cup.tmpl;

namespace {

using cup::test::TempDir;

// kBuiltinsOnly is the empty root that tells cup.tmpl to consult only the
// embedded corpus. (Go: passing "" for root.)
const std::filesystem::path kBuiltinsOnly;

[[nodiscard]] bool contains(const std::vector<std::string>& list, std::string_view want) {
    return std::ranges::find(list, want) != list.end();
}

}  // namespace

// Go: TestExistsBuiltin
TEST_CASE("exists finds built-in templates and rejects absent ones", "[tmpl][exists]") {
    REQUIRE(cup::tmpl::exists(kBuiltinsOnly, "headers", "class", "source.h.tmpl"));
    REQUIRE_FALSE(cup::tmpl::exists(kBuiltinsOnly, "headers", "class", "nope.tmpl"));
}

// Go: TestReadBuiltin
TEST_CASE("read returns built-in template content", "[tmpl][read]") {
    const auto got = cup::tmpl::read(kBuiltinsOnly, "headers", "class", "source.h.tmpl");
    REQUIRE(got.has_value());
    REQUIRE_FALSE(got->empty());
}

// Go: TestReadBuiltin's error path — Go returns embed.FS's error for a missing file.
TEST_CASE("read reports a missing template", "[tmpl][read]") {
    const auto got = cup::tmpl::read(kBuiltinsOnly, "headers", "class", "nope.tmpl");
    REQUIRE_FALSE(got.has_value());
}

// Go: TestIsCompiled
TEST_CASE("is_compiled distinguishes .h/.cpp pairs from header-only kinds", "[tmpl][compiled]") {
    // class ships a .h/.cpp pair -> compiled.
    REQUIRE(cup::tmpl::is_compiled(kBuiltinsOnly, "headers", "class"));
    // templated-class is header-only (.hpp) -> not compiled.
    REQUIRE_FALSE(cup::tmpl::is_compiled(kBuiltinsOnly, "headers", "templated-class"));
    // modules kinds are never compiled in the headers sense.
    REQUIRE_FALSE(cup::tmpl::is_compiled(kBuiltinsOnly, "modules", "class"));
}

// Go: TestKindsExcludesSpecialDirs
TEST_CASE("kinds lists component kinds and excludes the special dirs", "[tmpl][kinds]") {
    const auto kinds = cup::tmpl::kinds(kBuiltinsOnly, "headers");
    for (const auto* want : {"class", "interface", "templated-class"}) {
        INFO("expected component kind: " << want);
        REQUIRE(contains(kinds, want));
    }
    for (const auto* excluded : {"app", "test", "project"}) {
        INFO("special dir that must not be offered: " << excluded);
        REQUIRE_FALSE(contains(kinds, excluded));
    }
}

// Go: TestKindsModules
TEST_CASE("kinds finds modules-family kinds by their interface unit", "[tmpl][kinds]") {
    REQUIRE(contains(cup::tmpl::kinds(kBuiltinsOnly, "modules"), "class"));
}

// Go: TestBuiltinKindsIncludesSpecialDirs
TEST_CASE("builtin_kinds includes the special dirs", "[tmpl][kinds]") {
    const auto kinds = cup::tmpl::builtin_kinds("headers");
    for (const auto* want : {"app", "test", "project", "class"}) {
        INFO("expected built-in dir: " << want);
        REQUIRE(contains(kinds, want));
    }
}

// Go: TestProjectOverrideWins
TEST_CASE("a project-local template overrides the built-in", "[tmpl][override]") {
    const TempDir root;
    root.write(std::filesystem::path(cup::tmpl::kProjectTemplateDir) / "class" / "source.h.tmpl",
               "OVERRIDE");

    const auto got = cup::tmpl::read(root, "headers", "class", "source.h.tmpl");
    REQUIRE(got.has_value());
    REQUIRE(*got == "OVERRIDE");
    REQUIRE(cup::tmpl::exists(root, "headers", "class", "source.h.tmpl"));
}

// Go: TestProjectOverrideAddsNewKind
TEST_CASE("a project-local directory adds a new kind", "[tmpl][override][kinds]") {
    const TempDir root;
    // A brand-new headers component kind, defined only in the project.
    root.write(std::filesystem::path(cup::tmpl::kProjectTemplateDir) / "widget" / "source.hpp.tmpl",
               "// widget");

    REQUIRE(contains(cup::tmpl::kinds(root, "headers"), "widget"));
}

// Go: TestCopyBuiltin
TEST_CASE("copy_builtin writes every file of a built-in kind", "[tmpl][copy]") {
    const TempDir tmp;
    const std::filesystem::path dst = tmp.path() / "copied";

    REQUIRE(cup::tmpl::copy_builtin("headers", "class", dst).has_value());
    for (const auto* name : {"source.h.tmpl", "source.cpp.tmpl", "CMakeLists.txt.tmpl"}) {
        INFO("expected copied file: " << name);
        REQUIRE(std::filesystem::exists(dst / name));
    }
}

// Not in the Go suite: Go's CopyBuiltin surfaces embed.FS's ReadDir error for an
// unknown kind, so the C++ port must not silently create an empty directory.
TEST_CASE("copy_builtin reports an unknown kind", "[tmpl][copy]") {
    const TempDir tmp;
    REQUIRE_FALSE(cup::tmpl::copy_builtin("headers", "ghost", tmp.path() / "copied").has_value());
}
