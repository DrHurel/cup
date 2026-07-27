// Port of the compiler half of internal/scaffold/compiler_test.go — the release
// parsers that file also covers live in scaffold_releases_test.cpp, following the
// C++ partition split rather than the Go file split.
//
// The Go tests already pass against the Go implementation, so they define the
// behaviour precisely; each TEST_CASE below names the Go test it replaces. The
// golden-tree case at the bottom has no Go counterpart: it checks this
// implementation against bytes the *Go* one wrote.

#include <catch2/catch_test_macros.hpp>

// replace_compiler_guard returns std::expected<void, Error>, whose void
// specialisation a module cannot re-export from its global module fragment — see
// the note in ui_test.cpp.
#include <expected>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#include "TempDir.hpp"

import cup.scaffold;

namespace {

using cup::scaffold::Compilers;
using cup::test::TempDir;

[[nodiscard]] std::string slurp(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

}  // namespace

// Go: TestMinCompilers
TEST_CASE("min_compilers pins a baseline per standard", "[scaffold][compiler]") {
    REQUIRE(cup::scaffold::min_compilers(23) == Compilers{.gcc = 15, .clang = 17});
    REQUIRE(cup::scaffold::min_compilers(20) == Compilers{.gcc = 11, .clang = 16});
    REQUIRE(cup::scaffold::min_compilers(17) == Compilers{.gcc = 7, .clang = 5});
    REQUIRE(cup::scaffold::min_compilers(14) == Compilers{.gcc = 5, .clang = 4});
    REQUIRE(cup::scaffold::min_compilers(11) == Compilers{.gcc = 5, .clang = 4});
}

// Go: TestCompilerChoices
TEST_CASE("compiler_choices runs from the baseline to the newest", "[scaffold][compiler]") {
    // C++23's baseline GCC (15) equals the fetched newest, so it is the sole option.
    const auto cpp23 = cup::scaffold::compiler_choices(23, 15, 20);
    REQUIRE(cpp23.gcc == std::vector<int>{15});
    // Clang ranges from its baseline up to the newest, oldest first.
    REQUIRE(cpp23.clang == std::vector<int>{17, 18, 19, 20});

    // A lower standard opens up more (older) GCC options, baseline first, and the
    // ceiling tracks whatever newest was discovered.
    const auto cpp20 = cup::scaffold::compiler_choices(20, 16, 20);
    REQUIRE(cpp20.gcc.front() == 11);
    REQUIRE(cpp20.gcc.back() == 16);

    // A newest that lags the baseline (it should not happen, but the picker must
    // not come back empty) collapses to the baseline alone.
    REQUIRE(cup::scaffold::compiler_choices(23, 14, 20).gcc == std::vector<int>{15});
}

// Go: TestCompilerGuard
TEST_CASE("compiler_guard renders both branches inside the markers", "[scaffold][compiler]") {
    const std::string guard = cup::scaffold::compiler_guard(15, 17);
    INFO(guard);
    REQUIRE(guard.starts_with(cup::scaffold::kGuardStart));
    REQUIRE(guard.ends_with(cup::scaffold::kGuardEnd));

    for (const std::string_view want :
         {R"(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 15)",
          R"(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 17)",
          "requires GCC >= 15", "requires Clang >= 17",
          // CMake's own variable reference, which the format string has to escape.
          "${CMAKE_CXX_COMPILER_VERSION}"}) {
        INFO("expected in the guard: " << want);
        REQUIRE(guard.contains(want));
    }
}

// Go: TestCompilerGuardZeroDisables
TEST_CASE("a zero version drops that compiler's branch", "[scaffold][compiler]") {
    const std::string gcc_only = cup::scaffold::compiler_guard(15, 0);
    INFO(gcc_only);
    REQUIRE_FALSE(gcc_only.contains("Clang"));
    REQUIRE(gcc_only.contains("GNU"));

    const std::string none = cup::scaffold::compiler_guard(0, 0);
    INFO(none);
    REQUIRE_FALSE(none.contains("if("));
    REQUIRE_FALSE(none.contains("FATAL_ERROR"));
    // Still delimited, so `cup compiler set` can rewrite it later.
    REQUIRE(none.starts_with(cup::scaffold::kGuardStart));
    REQUIRE(none.ends_with(cup::scaffold::kGuardEnd));
}

// Go: TestReplaceCompilerGuard
TEST_CASE("replace_compiler_guard rewrites only the block", "[scaffold][compiler]") {
    const TempDir root;
    const std::filesystem::path path = root.path() / "CMakeLists.txt";
    root.write("CMakeLists.txt", "project(demo)\n\n" + cup::scaffold::compiler_guard(15, 17) +
                                     "\n\nset(CMAKE_CXX_STANDARD 23)\n");

    REQUIRE(cup::scaffold::replace_compiler_guard(root, path, 13, 0).has_value());

    const std::string written = slurp(path);
    INFO(written);
    REQUIRE(written.contains("VERSION_LESS 13"));
    // clang = 0 dropped the Clang branch entirely.
    REQUIRE_FALSE(written.contains("VERSION_LESS 17"));
    REQUIRE_FALSE(written.contains("Clang"));
    // The surrounding lines are untouched.
    REQUIRE(written.contains("project(demo)"));
    REQUIRE(written.contains("set(CMAKE_CXX_STANDARD 23)"));
}

// Go: TestReplaceCompilerGuardNoMarkers
TEST_CASE("replace_compiler_guard needs the markers", "[scaffold][compiler]") {
    const TempDir root;
    const std::filesystem::path path = root.path() / "CMakeLists.txt";
    root.write("CMakeLists.txt", "project(demo)\n");

    const auto replaced = cup::scaffold::replace_compiler_guard(root, path, 15, 17);
    REQUIRE_FALSE(replaced.has_value());
    REQUIRE(replaced.error().message().contains("compiler-guard"));

    // A file that is not there is a different failure, and also one.
    REQUIRE_FALSE(
        cup::scaffold::replace_compiler_guard(root, root.path() / "nope", 15, 17).has_value());
}

// No Go counterpart — this is the cross-implementation check, and the one case in
// this suite that could fail without any C++ change.
//
// internal/cmd/testdata/golden/ holds whole trees the *Go* cup produced, and Phase 5
// diffs trees from both binaries. The compiler guard is in every one of them and is
// rendered entirely by this partition, so it can be checked now rather than at the
// handover — byte for byte, against bytes this implementation did not write.
TEST_CASE("compiler_guard matches the Go cup's golden trees", "[scaffold][compiler][parity]") {
    const std::filesystem::path golden =
        std::filesystem::path(CUP_GO_GOLDEN_DIR) / "new" / "cmake-cpp20.txt";
    if (!std::filesystem::exists(golden)) {
        // Phase 6 deletes the Go implementation; the check retires with it rather
        // than turning into a failure.
        SUCCEED("no Go golden trees in this checkout");
        return;
    }

    // The snapshot format prefixes every content line with ">| " so file text can
    // never be confused with the manifest's own structure — see snapshotTree in
    // internal/cmd/golden_test.go.
    constexpr std::string_view kContentPrefix = ">| ";
    std::ifstream in(golden, std::ios::binary);
    REQUIRE(in);

    std::string extracted;
    bool inside = false;
    for (std::string line; std::getline(in, line);) {
        if (!line.starts_with(kContentPrefix)) {
            continue;
        }
        const std::string_view content = std::string_view(line).substr(kContentPrefix.size());
        if (content.starts_with(cup::scaffold::kGuardStart)) {
            inside = true;
        }
        if (!inside) {
            continue;
        }
        extracted += content;
        if (content.starts_with(cup::scaffold::kGuardEnd)) {
            break;
        }
        extracted += '\n';
    }

    REQUIRE_FALSE(extracted.empty());
    // cmake-cpp20 is scaffolded at the C++20 baseline, which is GCC 11 / Clang 16.
    REQUIRE(extracted == cup::scaffold::compiler_guard(11, 16));
}
