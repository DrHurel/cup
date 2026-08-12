#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <map>
#include <string>

#include "TempDir.hpp"

import cup.scaffold;
import cup.tmpl;

namespace {

using cup::test::TempDir;

// Drops a project-local override template so render()/tmpl::read read
// deterministic content instead of a built-in.
void write_template(const TempDir& root, std::string_view kind, std::string_view name,
                     std::string_view content) {
    root.write(std::filesystem::path(cup::tmpl::kProjectTemplateDir) / kind / name, content);
}

[[nodiscard]] std::string read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

}

TEST_CASE("render substitutes every {{key}}", "[render]") {
    const TempDir root;
    write_template(root, "class", "source.h.tmpl", "class {{Name}} in {{ns}} {};");

    const auto got = cup::scaffold::render(root, "headers", "class", "source.h.tmpl",
                                           {{"Name", "Widget"}, {"ns", "ui"}});
    REQUIRE(got.has_value());
    REQUIRE(*got == "class Widget in ui {};");
}

TEST_CASE("render resolves a placeholder nested inside a variable's value", "[render]") {
    const TempDir root;
    write_template(root, "greet", "hello.tmpl", "{{greeting}}");

    const auto got = cup::scaffold::render(root, "headers", "greet", "hello.tmpl",
                                           {{"greeting", "hi {{name}}"}, {"name", "cup"}});
    REQUIRE(got.has_value());
    REQUIRE(*got == "hi cup");
}

TEST_CASE("render reports every unresolved placeholder, deduplicated", "[render]") {
    const TempDir root;
    write_template(root, "class", "source.h.tmpl", "{{Name}} {{Missing}} {{Missing}}");

    const auto got =
        cup::scaffold::render(root, "headers", "class", "source.h.tmpl", {{"Name", "X"}});
    REQUIRE_FALSE(got.has_value());
    const std::string& message = got.error().message();
    const auto first = message.find("{{Missing}}");
    REQUIRE(first != std::string::npos);
    REQUIRE(message.find("{{Missing}}", first + 1) == std::string::npos);
}

TEST_CASE("render reports a missing template", "[render]") {
    const TempDir root;
    REQUIRE_FALSE(cup::scaffold::render(root, "headers", "nope", "source.h.tmpl", {}).has_value());
}

TEST_CASE("ensure_file creates a file but never overwrites an existing one", "[render]") {
    const TempDir root;
    const std::filesystem::path path = root.path() / "sub" / "dir" / "file.txt";

    REQUIRE(cup::scaffold::ensure_file(root, path, "first").has_value());
    REQUIRE(read_file(path) == "first");

    REQUIRE(cup::scaffold::ensure_file(root, path, "second").has_value());
    REQUIRE(read_file(path) == "first");
}

TEST_CASE("write_file creates nested directories for a fresh path without prompting",
          "[render]") {
    const TempDir root;
    const std::filesystem::path path = root.path() / "a" / "b" / "c.txt";

    // A fresh path never prompts, so this exercises the non-interactive path.
    const auto wrote = cup::scaffold::write_file(root, path, "content");
    REQUIRE(wrote.has_value());
    REQUIRE(*wrote);
    REQUIRE(read_file(path) == "content");
}
