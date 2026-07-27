// Port of internal/scaffold/std_test.go.
//
// The Go tests already pass against the Go implementation, so they define the
// behaviour precisely; each TEST_CASE below names the Go test it replaces.

#include <catch2/catch_test_macros.hpp>

// parse_std returns std::expected, and a module re-exports no declaration from its
// global module fragment — see the note in ui_test.cpp.
#include <expected>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

import cup.scaffold;

namespace {

using cup::scaffold::Vars;

}  // namespace

// Go: TestStdLabel
TEST_CASE("std_label renders the picker label", "[scaffold][std]") {
    REQUIRE(cup::scaffold::std_label(23) == "c++23");
    REQUIRE(cup::scaffold::std_label(20) == "c++20");
    REQUIRE(cup::scaffold::std_label(11) == "c++11");
}

// Go: TestParseStd
TEST_CASE("parse_std accepts labels and bare numbers", "[scaffold][std]") {
    const std::vector<std::pair<std::string_view, int>> valid{
        {"c++23", 23}, {"c++20", 20}, {"c++17", 17}, {"c++14", 14},
        {"c++11", 11}, {"23", 23},    {"11", 11},
    };
    for (const auto& [text, want] : valid) {
        INFO("parse_std(" << text << ")");
        const auto got = cup::scaffold::parse_std(text);
        REQUIRE(got.has_value());
        REQUIRE(*got == want);
    }

    // "42" and "99" are numbers cup does not scaffold; "2x" is the case a lenient
    // parse would accept as 2.
    for (const std::string_view text :
         {"", "c++99", "99", "42", "c++", "abc", "c++ab", "2x"}) {
        INFO("parse_std(" << text << ")");
        REQUIRE_FALSE(cup::scaffold::parse_std(text).has_value());
    }
}

// Go: TestParseStdErrorNamesInput
TEST_CASE("parse_std quotes the standard it rejected", "[scaffold][std]") {
    const auto got = cup::scaffold::parse_std("c++99");
    REQUIRE_FALSE(got.has_value());
    // The "c++" prefix is stripped before the value is quoted in the error.
    REQUIRE(got.error().message().contains("\"99\""));
}

// Go: TestUsesModules
TEST_CASE("uses_modules is true from C++20 up", "[scaffold][std]") {
    for (const int standard : {20, 23, 26}) {
        INFO("standard " << standard);
        REQUIRE(cup::scaffold::uses_modules(standard));
    }
    for (const int standard : {11, 14, 17}) {
        INFO("standard " << standard);
        REQUIRE_FALSE(cup::scaffold::uses_modules(standard));
    }
}

// Go: TestFamily
TEST_CASE("family selects the template subtree", "[scaffold][std]") {
    REQUIRE(cup::scaffold::family(23) == "modules");
    REQUIRE(cup::scaffold::family(20) == "modules");
    REQUIRE(cup::scaffold::family(17) == "headers");
    REQUIRE(cup::scaffold::family(14) == "headers");
    REQUIRE(cup::scaffold::family(11) == "headers");
}

// Go: TestStdVars
TEST_CASE("std_vars carries the standard into the templates", "[scaffold][std]") {
    struct Case {
        std::string_view name;
        int standard;
        // std_module is what the project asked for; below C++23 there is no std
        // module to grant, so asking cannot produce one.
        bool std_module;
        std::string_view lib;
        std::string_view hello;
        // modules is whether the module-only keys exist at all: the headers family
        // must not set them, even empty.
        bool modules;
        std::string_view prelude;
        std::string_view import_std;
    };

    const std::vector<Case> cases{
        {.name = "c++23 on the std module",
         .standard = 23,
         .std_module = true,
         .lib = "import std;",
         .hello = "std::println",
         // Blank lines around the import so the .cppm greeting keeps its spacing;
         // an empty prelude leaves nothing before the module declaration.
         .modules = true,
         .prelude = "",
         .import_std = "\nimport std;\n"},
        {// GCC 14 — and so cup's own build: std::println comes from the standard
         // <print>, in a global module fragment, and never from an `import std;`.
         .name = "c++23 without the std module",
         .standard = 23,
         .std_module = false,
         .lib = "#include <print>",
         .hello = "std::println",
         .modules = true,
         .prelude = "module;\n#include <print>\n",
         .import_std = ""},
        {.name = "c++20 cannot have the std module",
         .standard = 20,
         .std_module = true,
         .lib = "#include <iostream>",
         .hello = "std::cout",
         .modules = true,
         .prelude = "module;\n#include <iostream>\n",
         .import_std = ""},
        {.name = "c++17 headers",
         .standard = 17,
         .std_module = false,
         .lib = "#include <iostream>",
         .hello = "std::cout",
         .modules = false,
         .prelude = "",
         .import_std = ""},
    };

    for (const auto& test : cases) {
        INFO(test.name);
        const Vars vars = cup::scaffold::std_vars(test.standard, test.std_module);

        REQUIRE(vars.at("std_number") == std::to_string(test.standard));
        REQUIRE(vars.at("std_lib") == test.lib);
        REQUIRE(vars.at("hello").contains(test.hello));

        // The .cppm-only pair: present with the given values for the modules
        // family, absent entirely below C++20.
        REQUIRE(vars.contains("std_prelude") == test.modules);
        REQUIRE(vars.contains("std_import") == test.modules);
        if (test.modules) {
            REQUIRE(vars.at("std_prelude") == test.prelude);
            REQUIRE(vars.at("std_import") == test.import_std);
        }
    }
}

// No Go counterpart: kStandards is a var there and a constexpr array here, and the
// picker's order — newest first, first entry the default — is behaviour.
TEST_CASE("kStandards offers the newest standard first", "[scaffold][std]") {
    REQUIRE(cup::scaffold::kStandards.front() == 23);
    REQUIRE(cup::scaffold::kStandards.back() == 11);
    for (const int standard : cup::scaffold::kStandards) {
        INFO("standard " << standard);
        REQUIRE(cup::scaffold::parse_std(cup::scaffold::std_label(standard)).has_value());
    }
}
