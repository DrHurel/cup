#include <catch2/catch_test_macros.hpp>

#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <utility>

import cup.scaffold;

TEST_CASE("std_label renders the picker label", "[std][std_label]") {
    REQUIRE(cup::scaffold::std_label(23) == "c++23");
    REQUIRE(cup::scaffold::std_label(20) == "c++20");
    REQUIRE(cup::scaffold::std_label(11) == "c++11");
}

TEST_CASE("parse_std accepts prefixed and bare standards cup scaffolds",
          "[std][parse_std]") {
    const std::pair<std::string_view, int> valid[] = {
        {"c++23", 23}, {"c++20", 20}, {"c++17", 17},
        {"c++14", 14}, {"c++11", 11}, {"23", 23},
        {"11", 11},
    };
    for (const auto& [in, want] : valid) {
        INFO("input: " << in);
        const auto got = cup::scaffold::parse_std(in);
        REQUIRE(got.has_value());
        REQUIRE(*got == want);
    }
}

TEST_CASE("parse_std rejects anything cup does not scaffold", "[std][parse_std]") {
    for (const auto* s : {"", "c++99", "99", "42", "c++", "abc", "c++ab", "2x"}) {
        INFO("expected invalid: " << s);
        REQUIRE_FALSE(cup::scaffold::parse_std(s).has_value());
    }
}

TEST_CASE("parse_std's error names the (unprefixed) input", "[std][parse_std]") {
    const auto got = cup::scaffold::parse_std("c++99");
    REQUIRE_FALSE(got.has_value());
    REQUIRE(got.error().message().find("\"99\"") != std::string::npos);
}

TEST_CASE("uses_modules is true from C++20 up", "[std][uses_modules]") {
    for (const int std : {20, 23, 26}) {
        INFO("std: " << std);
        REQUIRE(cup::scaffold::uses_modules(std));
    }
    for (const int std : {11, 14, 17}) {
        INFO("std: " << std);
        REQUIRE_FALSE(cup::scaffold::uses_modules(std));
    }
}

TEST_CASE("family maps a standard onto its template family", "[std][family]") {
    REQUIRE(cup::scaffold::family(23) == "modules");
    REQUIRE(cup::scaffold::family(20) == "modules");
    REQUIRE(cup::scaffold::family(17) == "headers");
    REQUIRE(cup::scaffold::family(14) == "headers");
    REQUIRE(cup::scaffold::family(11) == "headers");
}

namespace {

// Checks the .cppm-only pair: present with the given values for the modules
// family, absent entirely below C++20.
void check_module_keys(const std::map<std::string, std::string, std::less<>>& v, bool modules,
                        std::string_view prelude, std::string_view import_std) {
    const bool has_prelude = v.contains("std_prelude");
    const bool has_import = v.contains("std_import");
    if (!modules) {
        REQUIRE_FALSE(has_prelude);
        REQUIRE_FALSE(has_import);
        return;
    }
    REQUIRE(has_prelude);
    REQUIRE(has_import);
    REQUIRE(v.at("std_prelude") == prelude);
    REQUIRE(v.at("std_import") == import_std);
}

}

TEST_CASE("std_vars renders the per-standard template variables", "[std][std_vars]") {
    struct Case {
        std::string name;
        int std;
        bool std_module;
        std::string lib;
        std::string hello;
        bool modules;
        std::string prelude;
        std::string import_std;
    };
    const Case cases[] = {
        {"c++23 on the std module", 23, true, "import std;", "std::println", true, "",
         "\nimport std;\n"},
        {"c++23 without the std module", 23, false, "#include <print>", "std::println", true,
         "module;\n#include <print>\n", ""},
        {"c++20 cannot have the std module", 20, true, "#include <iostream>", "std::cout", true,
         "module;\n#include <iostream>\n", ""},
        {"c++17 headers", 17, false, "#include <iostream>", "std::cout", false, "", ""},
    };

    for (const auto& c : cases) {
        SECTION(c.name) {
            const auto v = cup::scaffold::std_vars(c.std, c.std_module);
            REQUIRE(v.at("std_number") == std::to_string(c.std));
            REQUIRE(v.at("std_lib") == c.lib);
            REQUIRE(v.at("hello").find(c.hello) != std::string::npos);
            check_module_keys(v, c.modules, c.prelude, c.import_std);
        }
    }
}
