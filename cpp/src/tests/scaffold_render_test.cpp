// Port of internal/scaffold/render_test.go.
//
// The Go tests already pass against the Go implementation, so they define the
// behaviour precisely; each TEST_CASE below names the Go test it replaces.

#include <catch2/catch_test_macros.hpp>

// ensure_file returns std::expected<void, Error>, whose void specialisation a
// module cannot re-export from its global module fragment — see the note in
// ui_test.cpp.
#include <expected>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <sstream>
#include <string>
#include <string_view>

#include "TempDir.hpp"

import cup.scaffold;
import cup.tmpl;
import cup.ui;

namespace {

using cup::scaffold::Vars;
using cup::test::TempDir;

// write_template drops a project-local override so render() reads deterministic
// content instead of a built-in. (Go: writeTemplate.)
void write_template(const TempDir& root, std::string_view kind, std::string_view name,
                    std::string_view content) {
    root.write(std::filesystem::path(cup::tmpl::kProjectTemplateDir) / kind / name, content);
}

[[nodiscard]] std::string slurp(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

}  // namespace

// Go: TestRenderSubstitutes
TEST_CASE("render substitutes every placeholder", "[scaffold][render]") {
    const TempDir root;
    write_template(root, "class", "source.h.tmpl", "class {{Name}} in {{ns}} {};");

    const auto got = cup::scaffold::render(root, "headers", "class", "source.h.tmpl",
                                           Vars{{"Name", "Widget"}, {"ns", "ui"}});
    REQUIRE(got.has_value());
    REQUIRE(*got == "class Widget in ui {};");
}

// Go: TestRenderResolvesNestedPlaceholders
TEST_CASE("render resolves a placeholder inside a value", "[scaffold][render]") {
    const TempDir root;
    write_template(root, "greet", "hello.tmpl", "{{greeting}}");

    // This is what {{hello}} does in the real templates: its value embeds {{name}}.
    const auto got = cup::scaffold::render(root, "headers", "greet", "hello.tmpl",
                                           Vars{{"greeting", "hi {{name}}"}, {"name", "cup"}});
    REQUIRE(got.has_value());
    REQUIRE(*got == "hi cup");
}

// Go: TestRenderUnresolvedPlaceholder
TEST_CASE("render reports an unresolved placeholder once", "[scaffold][render]") {
    const TempDir root;
    write_template(root, "class", "source.h.tmpl", "{{Name}} {{Missing}} {{Missing}}");

    const auto got =
        cup::scaffold::render(root, "headers", "class", "source.h.tmpl", Vars{{"Name", "X"}});
    REQUIRE_FALSE(got.has_value());

    const std::string message = got.error().message();
    INFO(message);
    REQUIRE(message.contains("{{Missing}}"));
    // The unresolved set is deduplicated, so {{Missing}} appears exactly once.
    REQUIRE(message.find("{{Missing}}") == message.rfind("{{Missing}}"));
}

// Go: TestRenderMissingTemplate
TEST_CASE("render reports a missing template", "[scaffold][render]") {
    const TempDir root;
    const auto got = cup::scaffold::render(root, "headers", "nope", "source.h.tmpl", Vars{});
    REQUIRE_FALSE(got.has_value());
    REQUIRE(got.error().message().contains("nope/source.h.tmpl"));
}

// Go: TestEnsureFile
TEST_CASE("ensure_file creates once and never overwrites", "[scaffold][render]") {
    const TempDir root;
    const std::filesystem::path path = root.path() / "sub" / "dir" / "file.txt";

    REQUIRE(cup::scaffold::ensure_file(root, path, "first").has_value());
    REQUIRE(slurp(path) == "first");

    // A second call must leave the existing content alone.
    REQUIRE(cup::scaffold::ensure_file(root, path, "second").has_value());
    REQUIRE(slurp(path) == "first");
}

// Go: TestWriteFileCreatesNested
TEST_CASE("write_file creates missing parent directories", "[scaffold][render]") {
    const TempDir root;
    const std::filesystem::path path = root.path() / "a" / "b" / "c.txt";

    // A fresh path never prompts, so this exercises the non-interactive path.
    const auto wrote = cup::scaffold::write_file(root, path, "content");
    REQUIRE(wrote.has_value());
    REQUIRE(*wrote);
    REQUIRE(slurp(path) == "content");
}

// No Go counterpart: the overwrite prompt needs a terminal there, so the Go suite
// only reaches the fresh-file path. cup.ui's ScopedInput makes the other two
// branches scriptable, and they are the ones that decide whether a user's edits
// survive a re-run of `cup add`.
TEST_CASE("write_file asks before overwriting", "[scaffold][render][overwrite]") {
    const TempDir root;
    const std::filesystem::path path = root.path() / "existing.txt";
    REQUIRE(cup::scaffold::ensure_file(root, path, "original").has_value());

    SECTION("declining leaves the file as it was and reports not-written") {
        std::istringstream answers("n\n");
        const cup::ui::ScopedInput scoped(answers);

        const auto wrote = cup::scaffold::write_file(root, path, "replacement");
        REQUIRE(wrote.has_value());
        REQUIRE_FALSE(*wrote);
        REQUIRE(slurp(path) == "original");
    }

    SECTION("accepting replaces the content") {
        std::istringstream answers("y\n");
        const cup::ui::ScopedInput scoped(answers);

        const auto wrote = cup::scaffold::write_file(root, path, "replacement");
        REQUIRE(wrote.has_value());
        REQUIRE(*wrote);
        REQUIRE(slurp(path) == "replacement");
    }

    SECTION("an aborted prompt is an error, not a silent skip") {
        std::istringstream answers;  // end of input straight away
        const cup::ui::ScopedInput scoped(answers);

        const auto wrote = cup::scaffold::write_file(root, path, "replacement");
        REQUIRE_FALSE(wrote.has_value());
        REQUIRE(utils::error::is_abort(wrote.error()));
        REQUIRE(slurp(path) == "original");
    }
}
