#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "TempDir.hpp"

import cup.scaffold;

namespace {

using cup::test::TempDir;

[[nodiscard]] std::string read(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

}

TEST_CASE("min_compilers", "[compiler][min_compilers]") {
    struct Case {
        int std;
        int gcc;
        int clang;
    };
    for (const auto& c : {Case{23, 15, 17}, Case{20, 11, 16}, Case{17, 7, 5}, Case{14, 5, 4},
                          Case{11, 5, 4}}) {
        INFO("std: " << c.std);
        const auto [gcc, clang] = cup::scaffold::min_compilers(c.std);
        REQUIRE(gcc == c.gcc);
        REQUIRE(clang == c.clang);
    }
}

TEST_CASE("compiler_choices", "[compiler][compiler_choices]") {
    // C++23's baseline GCC (15) equals the fetched newest, so it is the sole option.
    {
        const auto [gcc, clang] = cup::scaffold::compiler_choices(23, 15, 20);
        REQUIRE(gcc == std::vector<int>{15});
        // Clang ranges from its baseline up to the newest, oldest first.
        REQUIRE_FALSE(clang.empty());
        REQUIRE(clang.front() == 17);
        REQUIRE(clang.back() == 20);
    }
    // A lower standard opens up more (older) GCC options, baseline first, and the
    // ceiling tracks whatever newest we discovered.
    {
        const auto [gcc, clang] = cup::scaffold::compiler_choices(20, 16, 20);
        (void)clang;
        REQUIRE(gcc.front() == 11);
        REQUIRE(gcc.back() == 16);
    }
    // A newest that lags the baseline (shouldn't happen, but be safe) collapses to
    // the baseline alone rather than yielding an empty list.
    {
        const auto [gcc, clang] = cup::scaffold::compiler_choices(23, 14, 20);
        (void)clang;
        REQUIRE(gcc == std::vector<int>{15});
    }
}

TEST_CASE("compiler_guard", "[compiler][compiler_guard]") {
    const std::string guard = cup::scaffold::compiler_guard(15, 17);
    REQUIRE(guard.starts_with(cup::scaffold::kGuardStart));
    REQUIRE(guard.ends_with(cup::scaffold::kGuardEnd));
    for (const auto* want : {
             R"(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 15)",
             R"(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 17)",
             "requires GCC >= 15",
             "requires Clang >= 17",
         }) {
        INFO("expected substring: " << want);
        REQUIRE(guard.find(want) != std::string::npos);
    }
}

TEST_CASE("compiler_guard: a zero version disables that compiler's branch",
          "[compiler][compiler_guard]") {
    // A zero version drops that compiler's branch; both zero drops the if entirely.
    const std::string only = cup::scaffold::compiler_guard(15, 0);
    REQUIRE(only.find("Clang") == std::string::npos);
    REQUIRE(only.find("GNU") != std::string::npos);

    const std::string none = cup::scaffold::compiler_guard(0, 0);
    REQUIRE(none.find("if(") == std::string::npos);
    REQUIRE(none.find("FATAL_ERROR") == std::string::npos);
    // Still delimited so `cup compiler` can rewrite it later.
    REQUIRE(none.starts_with(cup::scaffold::kGuardStart));
    REQUIRE(none.ends_with(cup::scaffold::kGuardEnd));
}

TEST_CASE("replace_compiler_guard", "[compiler][replace_compiler_guard]") {
    const TempDir root;
    const std::string path = (root.path() / "CMakeLists.txt").string();
    const std::string body = "project(demo)\n\n" + cup::scaffold::compiler_guard(15, 17) +
                             "\n\nset(CMAKE_CXX_STANDARD 23)\n";
    root.write("CMakeLists.txt", body);

    REQUIRE(cup::scaffold::replace_compiler_guard(root.path().string(), path, 13, 0).has_value());
    const std::string out = read(path);
    REQUIRE(out.find("VERSION_LESS 13") != std::string::npos);
    REQUIRE(out.find("VERSION_LESS 17") == std::string::npos);
    REQUIRE(out.find("Clang") == std::string::npos);
    // Surrounding lines are preserved.
    REQUIRE(out.find("project(demo)") != std::string::npos);
    REQUIRE(out.find("set(CMAKE_CXX_STANDARD 23)") != std::string::npos);
}

TEST_CASE("replace_compiler_guard on a file without markers",
          "[compiler][replace_compiler_guard]") {
    const TempDir root;
    const std::string path = (root.path() / "CMakeLists.txt").string();
    root.write("CMakeLists.txt", "project(demo)\n");

    REQUIRE_FALSE(
        cup::scaffold::replace_compiler_guard(root.path().string(), path, 15, 17).has_value());
}
