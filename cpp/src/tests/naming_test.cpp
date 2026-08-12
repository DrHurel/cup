#include <catch2/catch_test_macros.hpp>

import cup.scaffold;

TEST_CASE("validate_ident accepts legal C++ identifiers", "[naming][validate_ident]") {
    for (const auto* s : {"x", "Foo", "_hidden", "a1", "MyClass", "snake_case", "__", "A0_9z"}) {
        INFO("expected valid: " << s);
        REQUIRE(cup::scaffold::validate_ident(s).has_value());
    }
}

TEST_CASE("validate_ident rejects illegal C++ identifiers", "[naming][validate_ident]") {
    for (const auto* s : {"", "1abc", "has space", "has-hyphen", "dot.dot", "ns::name", "é"}) {
        INFO("expected invalid: " << s);
        REQUIRE_FALSE(cup::scaffold::validate_ident(s).has_value());
    }
}

TEST_CASE("validate_non_empty accepts non-blank strings", "[naming][validate_non_empty]") {
    REQUIRE(cup::scaffold::validate_non_empty("ok").has_value());
}

TEST_CASE("validate_non_empty rejects blank strings", "[naming][validate_non_empty]") {
    for (const auto* s : {"", " ", "\t", "\n  \t"}) {
        INFO("expected blank: " << s);
        REQUIRE_FALSE(cup::scaffold::validate_non_empty(s).has_value());
    }
}

TEST_CASE("capitalize upper-cases the first rune only", "[naming][capitalize]") {
    REQUIRE(cup::scaffold::capitalize("") == "");
    REQUIRE(cup::scaffold::capitalize("mylib") == "Mylib");
    REQUIRE(cup::scaffold::capitalize("Mylib") == "Mylib");
    REQUIRE(cup::scaffold::capitalize("a") == "A");
    REQUIRE(cup::scaffold::capitalize("aBC") == "ABC");
    REQUIRE(cup::scaffold::capitalize("_leading") == "_leading");
}

TEST_CASE("path_to_namespace joins segments below the top-level dir with ::",
          "[naming][path_to_namespace]") {
    const std::string src = "/proj/src";
    REQUIRE(cup::scaffold::path_to_namespace(src, "/proj/src/libs") == "");
    REQUIRE(cup::scaffold::path_to_namespace(src, "/proj/src/libs/utils") == "utils");
    REQUIRE(cup::scaffold::path_to_namespace(src, "/proj/src/libs/utils/json") == "utils::json");
    REQUIRE(cup::scaffold::path_to_namespace(src, "/proj/src/libs/my-lib") == "my_lib");
    REQUIRE(cup::scaffold::path_to_namespace(src, "/proj/src/apps/cli/tools") == "cli::tools");
}

TEST_CASE("path_to_module joins segments below the top-level dir with .",
          "[naming][path_to_module]") {
    const std::string src = "/proj/src";
    REQUIRE(cup::scaffold::path_to_module(src, "/proj/src/libs/utils") == "utils");
    REQUIRE(cup::scaffold::path_to_module(src, "/proj/src/libs/utils/json") == "utils.json");
    REQUIRE(cup::scaffold::path_to_module(src, "/proj/src/libs/my-lib/net") == "my_lib.net");
}
