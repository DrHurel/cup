// Port of internal/scaffold/naming_test.go.
//
// The Go tests already pass against the Go implementation, so they define the
// behaviour precisely; each TEST_CASE below names the Go test it replaces.

#include <catch2/catch_test_macros.hpp>

// The validators return std::expected<void, Error>, whose void specialisation a
// module cannot re-export from its global module fragment — see the note in
// ui_test.cpp.
#include <expected>
#include <filesystem>
#include <string_view>
#include <utility>
#include <vector>

import cup.scaffold;

// Go: TestValidateIdent
TEST_CASE("validate_ident accepts C++ identifiers only", "[scaffold][naming]") {
    for (const std::string_view name :
         {"x", "Foo", "_hidden", "a1", "MyClass", "snake_case", "__", "A0_9z"}) {
        INFO("validate_ident(" << name << ")");
        REQUIRE(cup::scaffold::validate_ident(name).has_value());
    }

    // "é" is the case that pins the check as ASCII: the Go implementation's regexp
    // rejects it, so this one has to as well.
    for (const std::string_view name :
         {"", "1abc", "has space", "has-hyphen", "dot.dot", "ns::name", "é"}) {
        INFO("validate_ident(" << name << ")");
        REQUIRE_FALSE(cup::scaffold::validate_ident(name).has_value());
    }
}

// Go: TestValidateNonEmpty
TEST_CASE("validate_non_empty rejects blank input", "[scaffold][naming]") {
    REQUIRE(cup::scaffold::validate_non_empty("ok").has_value());
    for (const std::string_view blank : {"", " ", "\t", "\n  \t"}) {
        INFO("validate_non_empty(" << blank << ")");
        REQUIRE_FALSE(cup::scaffold::validate_non_empty(blank).has_value());
    }
}

// Go: TestCapitalize
TEST_CASE("capitalize upper-cases only the first character", "[scaffold][naming]") {
    const std::vector<std::pair<std::string_view, std::string_view>> cases{
        {"", ""},   {"mylib", "Mylib"}, {"Mylib", "Mylib"},
        {"a", "A"}, {"aBC", "ABC"},     {"_leading", "_leading"},
    };
    for (const auto& [in, want] : cases) {
        INFO("capitalize(" << in << ")");
        REQUIRE(cup::scaffold::capitalize(in) == want);
    }
}

// Go: TestPathToNamespace
TEST_CASE("path_to_namespace joins the segments below src/<top>", "[scaffold][naming]") {
    const std::filesystem::path src("/proj/src");
    const std::vector<std::pair<std::string_view, std::string_view>> cases{
        {"/proj/src/libs", ""},              // top-level only, no sub-parts
        {"/proj/src/libs/utils", "utils"},   // single segment
        {"/proj/src/libs/utils/json", "utils::json"},
        {"/proj/src/libs/my-lib", "my_lib"},  // hyphen -> underscore
        {"/proj/src/apps/cli/tools", "cli::tools"},
    };
    for (const auto& [dir, want] : cases) {
        INFO("path_to_namespace(" << dir << ")");
        REQUIRE(cup::scaffold::path_to_namespace(src, std::filesystem::path(dir)) == want);
    }
}

// Go: TestPathToModule
TEST_CASE("path_to_module joins the same segments with dots", "[scaffold][naming]") {
    const std::filesystem::path src("/proj/src");
    const std::vector<std::pair<std::string_view, std::string_view>> cases{
        {"/proj/src/libs/utils", "utils"},
        {"/proj/src/libs/utils/json", "utils.json"},
        {"/proj/src/libs/my-lib/net", "my_lib.net"},
    };
    for (const auto& [dir, want] : cases) {
        INFO("path_to_module(" << dir << ")");
        REQUIRE(cup::scaffold::path_to_module(src, std::filesystem::path(dir)) == want);
    }
}

// No Go counterpart: filepath.Rel returns an error for an unrelated path and the
// Go code discards it, while lexically_relative returns an empty path. Both end at
// "no namespace", and this pins that they do.
TEST_CASE("an unrelated directory yields no namespace", "[scaffold][naming]") {
    REQUIRE(cup::scaffold::path_to_namespace("/proj/src", "relative/libs/utils").empty());
    REQUIRE(cup::scaffold::path_to_module("/proj/src", "/proj/src").empty());
}
