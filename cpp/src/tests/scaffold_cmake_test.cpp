// Port of internal/scaffold/cmake_test.go.
//
// The Go tests already pass against the Go implementation, so they define the
// behaviour precisely; each TEST_CASE below names the Go test it replaces.
//
// Every function under test is run twice wherever the Go test does: `cup add` and
// `cup register` are run repeatedly against the same tree, so idempotence is the
// property that keeps a build file from growing a duplicate line each time.

#include <catch2/catch_test_macros.hpp>

// The editors return std::expected<void, Error>, whose void specialisation a module
// cannot re-export from its global module fragment — see the note in ui_test.cpp.
#include <expected>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <regex>
#include <string>
#include <string_view>
#include <vector>

#include "TempDir.hpp"

import cup.scaffold;

namespace {

using cup::test::TempDir;

[[nodiscard]] std::string slurp(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// write_file drops content at root/name and returns the full path. (Go: writeFile.)
[[nodiscard]] std::filesystem::path write_file(const TempDir& root, std::string_view name,
                                               std::string_view content) {
    root.write(name, content);
    return root.path() / name;
}

// occurrences counts non-overlapping appearances of needle — the assertion behind
// every idempotence check.
[[nodiscard]] std::size_t occurrences(std::string_view text, std::string_view needle) {
    std::size_t count = 0;
    for (std::size_t at = text.find(needle); at != std::string_view::npos;
         at = text.find(needle, at + needle.size())) {
        ++count;
    }
    return count;
}

}  // namespace

// Go: TestReadFileLines
TEST_CASE("read_file_lines drops the trailing newline", "[scaffold][cmake]") {
    const TempDir root;

    // Missing file: nullopt, which is Go's ok == false.
    REQUIRE_FALSE(cup::scaffold::read_file_lines(root.path() / "nope").has_value());

    // Empty file: present, with no lines.
    const auto empty = cup::scaffold::read_file_lines(write_file(root, "empty", ""));
    REQUIRE(empty.has_value());
    REQUIRE(empty->empty());

    // Trailing newline trimmed, so there is no phantom empty final line.
    const auto lines = cup::scaffold::read_file_lines(write_file(root, "f", "a\nb\nc\n"));
    REQUIRE(lines.has_value());
    REQUIRE(*lines == std::vector<std::string>{"a", "b", "c"});
}

// Go: TestRemoveDir
TEST_CASE("remove_dir deletes a tree and ignores a missing one", "[scaffold][cmake]") {
    const TempDir root;
    std::filesystem::create_directories(root.path() / "tree" / "sub");

    cup::scaffold::remove_dir(root.path() / "tree");
    REQUIRE_FALSE(std::filesystem::exists(root.path() / "tree"));

    // A missing path is not a failure — nothing to catch, nothing to report.
    cup::scaffold::remove_dir(root.path() / "does-not-exist");
}

// Go: TestEnsureLine
TEST_CASE("ensure_line creates, appends and does not duplicate", "[scaffold][cmake]") {
    const TempDir root;
    const std::filesystem::path path = root.path() / "CMakeLists.txt";

    REQUIRE(cup::scaffold::ensure_line(root, path, "add_subdirectory(src)").has_value());
    REQUIRE(slurp(path) == "add_subdirectory(src)\n");

    REQUIRE(cup::scaffold::ensure_line(root, path, "add_subdirectory(tests)").has_value());
    REQUIRE(slurp(path) == "add_subdirectory(src)\nadd_subdirectory(tests)\n");

    REQUIRE(cup::scaffold::ensure_line(root, path, "add_subdirectory(src)").has_value());
    REQUIRE(occurrences(slurp(path), "add_subdirectory(src)") == 1);
}

// Go: TestEnsureLineBefore
TEST_CASE("ensure_line_before inserts at the anchor", "[scaffold][cmake]") {
    const TempDir root;
    const std::filesystem::path path =
        write_file(root, "CMakeLists.txt", "add_subdirectory(src)\nadd_subdirectory(libs)\n");

    REQUIRE(cup::scaffold::ensure_line_before(root, path, "add_subdirectory(third_party)",
                                              "add_subdirectory(src)")
                .has_value());
    REQUIRE(slurp(path) ==
            "add_subdirectory(third_party)\nadd_subdirectory(src)\nadd_subdirectory(libs)\n");

    REQUIRE(cup::scaffold::ensure_line_before(root, path, "add_subdirectory(third_party)",
                                              "add_subdirectory(src)")
                .has_value());
    REQUIRE(occurrences(slurp(path), "third_party") == 1);

    // A missing anchor falls back to appending.
    const std::filesystem::path other = write_file(root, "other.txt", "line1\n");
    REQUIRE(cup::scaffold::ensure_line_before(root, other, "inserted", "no-such-anchor")
                .has_value());
    REQUIRE(slurp(other) == "line1\ninserted\n");
}

// Go: TestRemoveLine
TEST_CASE("remove_line deletes every exact match", "[scaffold][cmake]") {
    const TempDir root;
    const std::filesystem::path path =
        write_file(root, "CMakeLists.txt", "keep\ndrop\nkeep\ndrop\n");

    const auto removed = cup::scaffold::remove_line(root, path, "drop");
    REQUIRE(removed.has_value());
    REQUIRE(*removed);
    REQUIRE(slurp(path) == "keep\nkeep\n");

    // An absent line reports false and leaves the file alone.
    const auto again = cup::scaffold::remove_line(root, path, "drop");
    REQUIRE(again.has_value());
    REQUIRE_FALSE(*again);

    // A missing file reports false rather than failing.
    const auto missing = cup::scaffold::remove_line(root, root.path() / "nope", "x");
    REQUIRE(missing.has_value());
    REQUIRE_FALSE(*missing);
}

// Go: TestRemoveMatchingLine
TEST_CASE("remove_matching_line matches the trimmed line", "[scaffold][cmake]") {
    const TempDir root;
    const std::filesystem::path path =
        write_file(root, "CMakeLists.txt", "add_dep(foo v1)\nkeep\n  add_dep(foo v2)\n");

    // The matcher owns its pattern — see LineMatcher in Cmake.cppm for why the
    // interface takes a predicate rather than a std::regex.
    const std::regex pattern(R"(^add_dep\(foo\b)");
    const auto matches = [&pattern](std::string_view line) {
        return std::regex_search(line.begin(), line.end(), pattern);
    };

    const auto removed = cup::scaffold::remove_matching_line(root, path, matches);
    REQUIRE(removed.has_value());
    REQUIRE(*removed);
    // Both matching lines are gone — the indented one too, since the line is
    // trimmed before it is offered — and "keep" stays.
    REQUIRE(slurp(path) == "keep\n");

    const std::regex never(R"(^zzz)");
    const auto no_match = cup::scaffold::remove_matching_line(
        root, path, [&never](std::string_view line) {
            return std::regex_search(line.begin(), line.end(), never);
        });
    REQUIRE(no_match.has_value());
    REQUIRE_FALSE(*no_match);

    const auto missing = cup::scaffold::remove_matching_line(root, root.path() / "nope", matches);
    REQUIRE(missing.has_value());
    REQUIRE_FALSE(*missing);
}

// Go: TestAppendBlock
TEST_CASE("append_block separates the block and skips a repeat", "[scaffold][cmake]") {
    const TempDir root;
    const std::filesystem::path path = write_file(root, "CMakeLists.txt", "project(demo)");

    const std::string_view block = "FetchContent_Declare(\n  fmt\n)\n";
    REQUIRE(cup::scaffold::append_block(root, path, "fmt", block).has_value());

    const std::string written = slurp(path);
    REQUIRE(written.contains("FetchContent_Declare"));
    // A blank line separates the prior content from the appended block.
    REQUIRE(written.contains("project(demo)\n\nFetchContent_Declare"));

    // The marker is already there: a repeat registration changes nothing.
    REQUIRE(cup::scaffold::append_block(root, path, "fmt", block).has_value());
    REQUIRE(slurp(path) == written);
}

// Go: TestAppendBlockNewFile
TEST_CASE("append_block writes a new file verbatim", "[scaffold][cmake]") {
    const TempDir root;
    const std::filesystem::path path = root.path() / "CMakeLists.txt";
    REQUIRE(cup::scaffold::append_block(root, path, "m", "block\n").has_value());
    REQUIRE(slurp(path) == "block\n");
}

// Go: TestRemoveFetchContentBlock
TEST_CASE("remove_fetch_content_block removes the whole declaration", "[scaffold][cmake]") {
    const TempDir root;
    const std::filesystem::path path = write_file(root, "CMakeLists.txt",
                                                  R"(project(demo)

FetchContent_Declare(
  fmt
  GIT_REPOSITORY https://github.com/fmtlib/fmt
)
FetchContent_MakeAvailable(fmt)

add_subdirectory(src)
)");

    const auto removed = cup::scaffold::remove_fetch_content_block(root, path, "fmt");
    REQUIRE(removed.has_value());
    REQUIRE(*removed);

    const std::string written = slurp(path);
    INFO(written);
    REQUIRE_FALSE(written.contains("FetchContent_Declare"));
    REQUIRE_FALSE(written.contains("fmt"));
    REQUIRE(written.contains("project(demo)"));
    REQUIRE(written.contains("add_subdirectory(src)"));

    const auto absent = cup::scaffold::remove_fetch_content_block(root, path, "absent");
    REQUIRE(absent.has_value());
    REQUIRE_FALSE(*absent);

    const auto missing =
        cup::scaffold::remove_fetch_content_block(root, root.path() / "nope", "fmt");
    REQUIRE(missing.has_value());
    REQUIRE_FALSE(*missing);
}

// Go: TestAddModuleSource
TEST_CASE("add_module_source appends at the block's indentation", "[scaffold][cmake]") {
    const TempDir root;
    const std::filesystem::path path = write_file(root, "CMakeLists.txt",
                                                  R"(add_library(mylib)
target_sources(mylib
  PUBLIC
  FILE_SET CXX_MODULES FILES
    mylib.cppm
)
)");

    REQUIRE(cup::scaffold::add_module_source(root, path, "extra.cppm").has_value());
    const std::string written = slurp(path);
    INFO(written);
    // Indentation matches the first entry (four spaces).
    REQUIRE(written.contains("    extra.cppm"));

    REQUIRE(cup::scaffold::add_module_source(root, path, "extra.cppm").has_value());
    REQUIRE(occurrences(slurp(path), "extra.cppm") == 1);
}

// Go: TestAddModuleSourceErrors
TEST_CASE("add_module_source needs a file and a FILE_SET block", "[scaffold][cmake]") {
    const TempDir root;
    REQUIRE_FALSE(
        cup::scaffold::add_module_source(root, root.path() / "nope", "x.cppm").has_value());

    const std::filesystem::path path = write_file(root, "CMakeLists.txt", "add_library(mylib)\n");
    REQUIRE_FALSE(cup::scaffold::add_module_source(root, path, "x.cppm").has_value());
}

// Go: TestAddHeaderSource
TEST_CASE("add_header_source appends past BASE_DIRS", "[scaffold][cmake]") {
    const TempDir root;
    const std::filesystem::path path = write_file(root, "CMakeLists.txt",
                                                  R"(add_library(mylib INTERFACE)
target_sources(mylib
  PUBLIC
  FILE_SET HEADERS
  BASE_DIRS include
  FILES
    mylib/a.hpp
)
)");

    REQUIRE(cup::scaffold::add_header_source(root, path, "mylib/b.hpp").has_value());
    REQUIRE(slurp(path).contains("    mylib/b.hpp"));

    REQUIRE(cup::scaffold::add_header_source(root, path, "mylib/b.hpp").has_value());
    REQUIRE(occurrences(slurp(path), "mylib/b.hpp") == 1);
}

// Go: TestAddHeaderSourceErrors
TEST_CASE("add_header_source needs a file and a FILE_SET block", "[scaffold][cmake]") {
    const TempDir root;
    REQUIRE_FALSE(
        cup::scaffold::add_header_source(root, root.path() / "nope", "x.hpp").has_value());

    const std::filesystem::path path = write_file(root, "CMakeLists.txt", "add_library(mylib)\n");
    REQUIRE_FALSE(cup::scaffold::add_header_source(root, path, "x.hpp").has_value());
}

// Go: TestEnsureHeaderLibStatic
TEST_CASE("ensure_header_lib_static rescopes INTERFACE to PUBLIC", "[scaffold][cmake]") {
    const TempDir root;
    const std::filesystem::path path = write_file(root, "CMakeLists.txt",
                                                  R"(add_library(mylib INTERFACE)
target_include_directories(mylib INTERFACE include)
target_compile_features(mylib INTERFACE cxx_std_20)
)");

    REQUIRE(cup::scaffold::ensure_header_lib_static(root, path, "mylib").has_value());
    const std::string written = slurp(path);
    INFO(written);
    REQUIRE(written.contains("add_library(mylib STATIC)"));
    REQUIRE_FALSE(written.contains("INTERFACE"));
    REQUIRE(occurrences(written, "PUBLIC") == 2);

    // Already STATIC: a second call is a no-op.
    REQUIRE(cup::scaffold::ensure_header_lib_static(root, path, "mylib").has_value());
    REQUIRE(slurp(path) == written);
}

// Go: TestEnsureHeaderLibStaticMissing
TEST_CASE("ensure_header_lib_static reports a missing file", "[scaffold][cmake]") {
    const TempDir root;
    REQUIRE_FALSE(
        cup::scaffold::ensure_header_lib_static(root, root.path() / "nope", "mylib").has_value());
}

// Go: TestAddPartitionImport
TEST_CASE("add_partition_import starts and then extends the block", "[scaffold][cmake]") {
    const TempDir root;
    const std::filesystem::path primary =
        write_file(root, "mylib.cppm", "export module mylib;\n\nint x;\n");

    REQUIRE(cup::scaffold::add_partition_import(root, primary, "parser").has_value());
    std::string written = slurp(primary);
    INFO(written);
    // A fresh block sits right after the module declaration, separated by a blank
    // line.
    REQUIRE(written.contains("export module mylib;\n\nexport import :parser;"));

    // A second partition joins the existing block rather than starting another.
    REQUIRE(cup::scaffold::add_partition_import(root, primary, "lexer").has_value());
    written = slurp(primary);
    INFO(written);
    REQUIRE(written.contains("export import :parser;\nexport import :lexer;"));

    REQUIRE(cup::scaffold::add_partition_import(root, primary, "parser").has_value());
    REQUIRE(occurrences(slurp(primary), ":parser;") == 1);
}

// Go: TestAddPartitionImportErrors
TEST_CASE("add_partition_import needs a module declaration", "[scaffold][cmake]") {
    const TempDir root;
    REQUIRE_FALSE(cup::scaffold::add_partition_import(root, root.path() / "nope", "p").has_value());

    const std::filesystem::path primary = write_file(root, "bad.cppm", "int x;\n");
    REQUIRE_FALSE(cup::scaffold::add_partition_import(root, primary, "p").has_value());
}

// Go: TestListSubdirs
TEST_CASE("list_subdirs returns sorted directory names", "[scaffold][cmake]") {
    const TempDir root;
    for (const auto* name : {"zeta", "alpha", "mid"}) {
        std::filesystem::create_directories(root.path() / name);
    }
    root.write("file.txt", "x");  // a regular file must be ignored

    REQUIRE(cup::scaffold::list_subdirs(root) == std::vector<std::string>{"alpha", "mid", "zeta"});
    // A missing directory contributes nothing.
    REQUIRE(cup::scaffold::list_subdirs(root.path() / "nope").empty());
}
