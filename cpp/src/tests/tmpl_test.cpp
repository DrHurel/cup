
#include <catch2/catch_test_macros.hpp>

#include <unistd.h>

#include <algorithm>
#include <expected>
#include <filesystem>
#include <format>
#include <string>
#include <vector>

#include "TempDir.hpp"

import cup.tmpl;

namespace {

using cup::test::TempDir;

const std::filesystem::path kBuiltinsOnly;

[[nodiscard]] bool contains(const std::vector<std::string>& list, std::string_view want) {
    return std::ranges::find(list, want) != list.end();
}

}

TEST_CASE("exists finds built-in templates and rejects absent ones", "[tmpl][exists]") {
    REQUIRE(cup::tmpl::exists(kBuiltinsOnly, "headers", "class", "source.h.tmpl"));
    REQUIRE_FALSE(cup::tmpl::exists(kBuiltinsOnly, "headers", "class", "nope.tmpl"));
}

TEST_CASE("read returns built-in template content", "[tmpl][read]") {
    const auto got = cup::tmpl::read(kBuiltinsOnly, "headers", "class", "source.h.tmpl");
    REQUIRE(got.has_value());
    REQUIRE_FALSE(got->empty());
}

TEST_CASE("read reports a missing template", "[tmpl][read]") {
    const auto got = cup::tmpl::read(kBuiltinsOnly, "headers", "class", "nope.tmpl");
    REQUIRE_FALSE(got.has_value());
}

TEST_CASE("is_compiled distinguishes .h/.cpp pairs from header-only kinds", "[tmpl][compiled]") {
    REQUIRE(cup::tmpl::is_compiled(kBuiltinsOnly, "headers", "class"));
    REQUIRE_FALSE(cup::tmpl::is_compiled(kBuiltinsOnly, "headers", "templated-class"));
    REQUIRE_FALSE(cup::tmpl::is_compiled(kBuiltinsOnly, "modules", "class"));
}

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

TEST_CASE("kinds finds modules-family kinds by their interface unit", "[tmpl][kinds]") {
    REQUIRE(contains(cup::tmpl::kinds(kBuiltinsOnly, "modules"), "class"));
}

TEST_CASE("builtin_kinds includes the special dirs", "[tmpl][kinds]") {
    const auto kinds = cup::tmpl::builtin_kinds("headers");
    for (const auto* want : {"app", "test", "project", "class"}) {
        INFO("expected built-in dir: " << want);
        REQUIRE(contains(kinds, want));
    }
}

TEST_CASE("a project-local template overrides the built-in", "[tmpl][override]") {
    const TempDir root;
    root.write(std::filesystem::path(cup::tmpl::kProjectTemplateDir) / "class" / "source.h.tmpl",
               "OVERRIDE");

    const auto got = cup::tmpl::read(root, "headers", "class", "source.h.tmpl");
    REQUIRE(got.has_value());
    REQUIRE(*got == "OVERRIDE");
    REQUIRE(cup::tmpl::exists(root, "headers", "class", "source.h.tmpl"));
}

TEST_CASE("a project-local directory adds a new kind", "[tmpl][override][kinds]") {
    const TempDir root;
    root.write(std::filesystem::path(cup::tmpl::kProjectTemplateDir) / "widget" / "source.hpp.tmpl",
               "// widget");

    REQUIRE(contains(cup::tmpl::kinds(root, "headers"), "widget"));
}

TEST_CASE("kinds ignores project template entries that are not usable kinds",
          "[tmpl][kinds][override]") {
    const TempDir root;
    const std::filesystem::path templates(cup::tmpl::kProjectTemplateDir);
    root.write(templates / "notes.md", "not a kind");
    root.write(templates / "empty-kind" / "README.md", "no source here");

    const auto kinds = cup::tmpl::kinds(root, "headers");
    REQUIRE_FALSE(contains(kinds, "notes.md"));
    REQUIRE_FALSE(contains(kinds, "empty-kind"));
    REQUIRE(contains(kinds, "class"));
}

TEST_CASE("is_compiled needs both halves of the declaration/definition pair",
          "[tmpl][compiled][override]") {
    const TempDir root;
    root.write(std::filesystem::path(cup::tmpl::kProjectTemplateDir) / "halfclass" /
                   "source.h.tmpl",
               "// declaration only");

    REQUIRE(cup::tmpl::exists(root, "headers", "halfclass", "source.h.tmpl"));
    REQUIRE_FALSE(cup::tmpl::exists(root, "headers", "halfclass", "source.cpp.tmpl"));
    REQUIRE_FALSE(cup::tmpl::is_compiled(root, "headers", "halfclass"));
    REQUIRE_FALSE(contains(cup::tmpl::kinds(root, "headers"), "halfclass"));
}

TEST_CASE("copy_builtin writes every file of a built-in kind", "[tmpl][copy]") {
    const TempDir tmp;
    const std::filesystem::path dst = tmp.path() / "copied";

    REQUIRE(cup::tmpl::copy_builtin("headers", "class", dst).has_value());
    for (const auto* name : {"source.h.tmpl", "source.cpp.tmpl", "CMakeLists.txt.tmpl"}) {
        INFO("expected copied file: " << name);
        REQUIRE(std::filesystem::exists(dst / name));
    }
}

TEST_CASE("copy_builtin reports an unknown kind", "[tmpl][copy]") {
    const TempDir tmp;
    REQUIRE_FALSE(cup::tmpl::copy_builtin("headers", "ghost", tmp.path() / "copied").has_value());
}

TEST_CASE("an unreadable override falls through to the built-in", "[tmpl][override]") {
    if (::geteuid() == 0) {
        SKIP("root ignores the permission bits, so the override stays readable");
    }
    const TempDir root;
    const std::filesystem::path override_file =
        std::filesystem::path(cup::tmpl::kProjectTemplateDir) / "class" / "source.h.tmpl";
    root.write(override_file, "OVERRIDE");
    std::filesystem::permissions(root.path() / override_file, std::filesystem::perms::none);

    REQUIRE(cup::tmpl::exists(root, "headers", "class", "source.h.tmpl"));

    const auto got = cup::tmpl::read(root, "headers", "class", "source.h.tmpl");
    REQUIRE(got.has_value());
    REQUIRE(*got != "OVERRIDE");
    REQUIRE(*got == cup::tmpl::read(kBuiltinsOnly, "headers", "class", "source.h.tmpl").value());
}

TEST_CASE("copy_builtin reports a destination it cannot create", "[tmpl][copy]") {
    const TempDir tmp;
    tmp.write("blocker", "not a directory");

    const auto copied = cup::tmpl::copy_builtin("headers", "class", tmp.path() / "blocker" / "sub");
    REQUIRE_FALSE(copied.has_value());
    REQUIRE(copied.error().message().starts_with("creating "));
}

TEST_CASE("copy_builtin reports a file it cannot write", "[tmpl][copy]") {
    const TempDir tmp;
    const std::filesystem::path dst = tmp.path() / "copied";
    std::filesystem::create_directories(dst / "source.h.tmpl");

    const auto copied = cup::tmpl::copy_builtin("headers", "class", dst);
    REQUIRE_FALSE(copied.has_value());
    REQUIRE(copied.error().message() ==
            std::format("writing {}", (dst / "source.h.tmpl").string()));
}
