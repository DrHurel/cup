#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "TempDir.hpp"

import cup.scaffold;

namespace {

using cup::test::TempDir;

// Writes content to root/name (creating parent dirs) and returns the full path.
[[nodiscard]] std::string write_file(const TempDir& root, const std::string& name,
                                     std::string_view content) {
    root.write(name, content);
    return (root.path() / name).string();
}

[[nodiscard]] std::string read(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

}

TEST_CASE("read_file_lines", "[cmake][read_file_lines]") {
    const TempDir root;

    SECTION("missing file: nullopt") {
        REQUIRE_FALSE(
            cup::scaffold::read_file_lines((root.path() / "nope").string()).has_value());
    }

    SECTION("empty file: has_value true, no lines") {
        const auto lines = cup::scaffold::read_file_lines(write_file(root, "empty", ""));
        REQUIRE(lines.has_value());
        REQUIRE(lines->empty());
    }

    SECTION("trailing newline is trimmed, so no phantom empty final line") {
        const auto lines = cup::scaffold::read_file_lines(write_file(root, "f", "a\nb\nc\n"));
        REQUIRE(lines.has_value());
        REQUIRE(*lines == std::vector<std::string>{"a", "b", "c"});
    }
}

TEST_CASE("remove_dir", "[cmake][remove_dir]") {
    const TempDir root;
    std::filesystem::create_directories(root.path() / "tree" / "sub");

    cup::scaffold::remove_dir((root.path() / "tree").string());
    REQUIRE_FALSE(std::filesystem::exists(root.path() / "tree"));

    // Missing path is ignored (no panic / error surfaced).
    cup::scaffold::remove_dir((root.path() / "does-not-exist").string());
}

TEST_CASE("ensure_line", "[cmake][ensure_line]") {
    const TempDir root;
    const std::string path = (root.path() / "CMakeLists.txt").string();

    // Creates the file when absent.
    REQUIRE(cup::scaffold::ensure_line(root.path().string(), path, "add_subdirectory(src)")
                .has_value());
    REQUIRE(read(path) == "add_subdirectory(src)\n");

    // Appends a distinct line.
    REQUIRE(cup::scaffold::ensure_line(root.path().string(), path, "add_subdirectory(tests)")
                .has_value());
    REQUIRE(read(path) == "add_subdirectory(src)\nadd_subdirectory(tests)\n");

    // Idempotent: an existing line is not duplicated.
    REQUIRE(cup::scaffold::ensure_line(root.path().string(), path, "add_subdirectory(src)")
                .has_value());
    const std::string content = read(path);
    std::size_t occurrences = 0;
    for (std::size_t pos = content.find("add_subdirectory(src)"); pos != std::string::npos;
         pos = content.find("add_subdirectory(src)", pos + 1)) {
        ++occurrences;
    }
    REQUIRE(occurrences == 1);
}

TEST_CASE("ensure_line_before", "[cmake][ensure_line_before]") {
    const TempDir root;

    // Inserts immediately before the anchor.
    const std::string path =
        write_file(root, "CMakeLists.txt", "add_subdirectory(src)\nadd_subdirectory(libs)\n");
    REQUIRE(cup::scaffold::ensure_line_before(root.path().string(), path,
                                              "add_subdirectory(third_party)",
                                              "add_subdirectory(src)")
                .has_value());
    REQUIRE(read(path) == "add_subdirectory(third_party)\nadd_subdirectory(src)\n"
                          "add_subdirectory(libs)\n");

    // Idempotent.
    REQUIRE(cup::scaffold::ensure_line_before(root.path().string(), path,
                                              "add_subdirectory(third_party)",
                                              "add_subdirectory(src)")
                .has_value());
    std::size_t occurrences = 0;
    const std::string content = read(path);
    for (std::size_t pos = content.find("third_party"); pos != std::string::npos;
         pos = content.find("third_party", pos + 1)) {
        ++occurrences;
    }
    REQUIRE(occurrences == 1);

    // Missing anchor falls back to appending.
    const std::string path2 = write_file(root, "other.txt", "line1\n");
    REQUIRE(cup::scaffold::ensure_line_before(root.path().string(), path2, "inserted",
                                              "no-such-anchor")
                .has_value());
    REQUIRE(read(path2) == "line1\ninserted\n");
}

TEST_CASE("remove_line", "[cmake][remove_line]") {
    const TempDir root;
    const std::string path = write_file(root, "CMakeLists.txt", "keep\ndrop\nkeep\ndrop\n");

    auto removed = cup::scaffold::remove_line(root.path().string(), path, "drop");
    REQUIRE(removed.has_value());
    REQUIRE(*removed);
    REQUIRE(read(path) == "keep\nkeep\n");

    // Removing an absent line reports false and leaves the file unchanged.
    removed = cup::scaffold::remove_line(root.path().string(), path, "drop");
    REQUIRE(removed.has_value());
    REQUIRE_FALSE(*removed);

    // Missing file reports false, no error.
    removed = cup::scaffold::remove_line(root.path().string(), (root.path() / "nope").string(), "x");
    REQUIRE(removed.has_value());
    REQUIRE_FALSE(*removed);
}

TEST_CASE("remove_matching_line", "[cmake][remove_matching_line]") {
    const TempDir root;
    const std::string path =
        write_file(root, "CMakeLists.txt", "add_dep(foo v1)\nkeep\n  add_dep(foo v2)\n");

    auto removed =
        cup::scaffold::remove_matching_line(root.path().string(), path, R"(^add_dep\(foo\b)");
    REQUIRE(removed.has_value());
    REQUIRE(*removed);
    // Both matching lines gone (leading indent is trimmed before matching), keep stays.
    REQUIRE(read(path) == "keep\n");

    // No match: false, unchanged.
    removed = cup::scaffold::remove_matching_line(root.path().string(), path, "^zzz");
    REQUIRE(removed.has_value());
    REQUIRE_FALSE(*removed);

    // Missing file: false, no error.
    removed = cup::scaffold::remove_matching_line(root.path().string(),
                                                   (root.path() / "nope").string(),
                                                   R"(^add_dep\(foo\b)");
    REQUIRE(removed.has_value());
    REQUIRE_FALSE(*removed);
}

TEST_CASE("append_block", "[cmake][append_block]") {
    const TempDir root;
    const std::string path = write_file(root, "CMakeLists.txt", "project(demo)");

    const std::string block = "FetchContent_Declare(\n  fmt\n)\n";
    REQUIRE(cup::scaffold::append_block(root.path().string(), path, "fmt", block).has_value());
    std::string got = read(path);
    REQUIRE(got.find("FetchContent_Declare") != std::string::npos);
    // A blank line separates the prior content from the appended block.
    REQUIRE(got.find("project(demo)\n\nFetchContent_Declare") != std::string::npos);

    // Marker already present: no-op.
    const std::string before = got;
    REQUIRE(cup::scaffold::append_block(root.path().string(), path, "fmt", block).has_value());
    REQUIRE(read(path) == before);
}

TEST_CASE("append_block on a nonexistent file", "[cmake][append_block]") {
    const TempDir root;
    const std::string path = (root.path() / "CMakeLists.txt").string();
    REQUIRE(cup::scaffold::append_block(root.path().string(), path, "m", "block\n").has_value());
    REQUIRE(read(path) == "block\n");
}

TEST_CASE("remove_fetch_content_block", "[cmake][remove_fetch_content_block]") {
    const TempDir root;
    const std::string content = R"(project(demo)

FetchContent_Declare(
  fmt
  GIT_REPOSITORY https://github.com/fmtlib/fmt
)
FetchContent_MakeAvailable(fmt)

add_subdirectory(src)
)";
    const std::string path = write_file(root, "CMakeLists.txt", content);

    auto removed = cup::scaffold::remove_fetch_content_block(root.path().string(), path, "fmt");
    REQUIRE(removed.has_value());
    REQUIRE(*removed);
    const std::string got = read(path);
    REQUIRE(got.find("FetchContent_Declare") == std::string::npos);
    REQUIRE(got.find("fmt") == std::string::npos);
    REQUIRE(got.find("project(demo)") != std::string::npos);
    REQUIRE(got.find("add_subdirectory(src)") != std::string::npos);

    // Absent name: false.
    removed = cup::scaffold::remove_fetch_content_block(root.path().string(), path, "absent");
    REQUIRE(removed.has_value());
    REQUIRE_FALSE(*removed);

    // Missing file: false, no error.
    removed = cup::scaffold::remove_fetch_content_block(root.path().string(),
                                                         (root.path() / "nope").string(), "fmt");
    REQUIRE(removed.has_value());
    REQUIRE_FALSE(*removed);
}

TEST_CASE("add_module_source", "[cmake][add_module_source]") {
    const TempDir root;
    const std::string content = R"(add_library(mylib)
target_sources(mylib
  PUBLIC
  FILE_SET CXX_MODULES FILES
    mylib.cppm
)
)";
    const std::string path = write_file(root, "CMakeLists.txt", content);

    REQUIRE(cup::scaffold::add_module_source(root.path().string(), path, "extra.cppm").has_value());
    std::string got = read(path);
    REQUIRE(got.find("extra.cppm") != std::string::npos);
    // Indentation matches the first entry (4 spaces).
    REQUIRE(got.find("    extra.cppm") != std::string::npos);

    // Idempotent.
    REQUIRE(cup::scaffold::add_module_source(root.path().string(), path, "extra.cppm").has_value());
    got = read(path);
    std::size_t occurrences = 0;
    for (std::size_t pos = got.find("extra.cppm"); pos != std::string::npos;
         pos = got.find("extra.cppm", pos + 1)) {
        ++occurrences;
    }
    REQUIRE(occurrences == 1);
}

TEST_CASE("add_module_source errors", "[cmake][add_module_source]") {
    const TempDir root;

    // Missing file.
    REQUIRE_FALSE(cup::scaffold::add_module_source(root.path().string(),
                                                    (root.path() / "nope").string(), "x.cppm")
                      .has_value());

    // No FILE_SET block.
    const std::string path = write_file(root, "CMakeLists.txt", "add_library(mylib)\n");
    REQUIRE_FALSE(cup::scaffold::add_module_source(root.path().string(), path, "x.cppm").has_value());
}

TEST_CASE("add_header_source", "[cmake][add_header_source]") {
    const TempDir root;
    const std::string content = R"(add_library(mylib INTERFACE)
target_sources(mylib
  PUBLIC
  FILE_SET HEADERS
  BASE_DIRS include
  FILES
    mylib/a.hpp
)
)";
    const std::string path = write_file(root, "CMakeLists.txt", content);

    REQUIRE(
        cup::scaffold::add_header_source(root.path().string(), path, "mylib/b.hpp").has_value());
    REQUIRE(read(path).find("    mylib/b.hpp") != std::string::npos);

    // Idempotent.
    REQUIRE(
        cup::scaffold::add_header_source(root.path().string(), path, "mylib/b.hpp").has_value());
    const std::string got = read(path);
    std::size_t occurrences = 0;
    for (std::size_t pos = got.find("mylib/b.hpp"); pos != std::string::npos;
         pos = got.find("mylib/b.hpp", pos + 1)) {
        ++occurrences;
    }
    REQUIRE(occurrences == 1);
}

TEST_CASE("add_header_source errors", "[cmake][add_header_source]") {
    const TempDir root;
    REQUIRE_FALSE(cup::scaffold::add_header_source(root.path().string(),
                                                    (root.path() / "nope").string(), "x.hpp")
                      .has_value());
    const std::string path = write_file(root, "CMakeLists.txt", "add_library(mylib)\n");
    REQUIRE_FALSE(cup::scaffold::add_header_source(root.path().string(), path, "x.hpp").has_value());
}

TEST_CASE("ensure_header_lib_static", "[cmake][ensure_header_lib_static]") {
    const TempDir root;
    const std::string content = R"(add_library(mylib INTERFACE)
target_include_directories(mylib INTERFACE include)
target_compile_features(mylib INTERFACE cxx_std_20)
)";
    const std::string path = write_file(root, "CMakeLists.txt", content);

    REQUIRE(cup::scaffold::ensure_header_lib_static(root.path().string(), path, "mylib").has_value());
    const std::string got = read(path);
    REQUIRE(got.find("add_library(mylib STATIC)") != std::string::npos);
    REQUIRE(got.find("INTERFACE") == std::string::npos);
    std::size_t occurrences = 0;
    for (std::size_t pos = got.find("PUBLIC"); pos != std::string::npos;
         pos = got.find("PUBLIC", pos + 1)) {
        ++occurrences;
    }
    REQUIRE(occurrences == 2);

    // Already STATIC: no-op.
    const std::string before = read(path);
    REQUIRE(cup::scaffold::ensure_header_lib_static(root.path().string(), path, "mylib").has_value());
    REQUIRE(read(path) == before);
}

TEST_CASE("ensure_header_lib_static on a missing file", "[cmake][ensure_header_lib_static]") {
    const TempDir root;
    REQUIRE_FALSE(cup::scaffold::ensure_header_lib_static(root.path().string(),
                                                           (root.path() / "nope").string(), "mylib")
                      .has_value());
}

TEST_CASE("add_partition_import", "[cmake][add_partition_import]") {
    const TempDir root;

    // Fresh block: inserted after the module declaration with a blank line.
    const std::string primary =
        write_file(root, "mylib.cppm", "export module mylib;\n\nint x;\n");
    REQUIRE(cup::scaffold::add_partition_import(root.path().string(), primary, "parser").has_value());
    std::string got = read(primary);
    REQUIRE(got.find("export import :parser;") != std::string::npos);
    REQUIRE(got.find("export module mylib;\n\nexport import :parser;") != std::string::npos);

    // Second partition: appended after the last existing import.
    REQUIRE(cup::scaffold::add_partition_import(root.path().string(), primary, "lexer").has_value());
    got = read(primary);
    REQUIRE(got.find("export import :parser;\nexport import :lexer;") != std::string::npos);

    // Idempotent.
    REQUIRE(cup::scaffold::add_partition_import(root.path().string(), primary, "parser").has_value());
    got = read(primary);
    std::size_t occurrences = 0;
    for (std::size_t pos = got.find(":parser;"); pos != std::string::npos;
         pos = got.find(":parser;", pos + 1)) {
        ++occurrences;
    }
    REQUIRE(occurrences == 1);
}

TEST_CASE("add_partition_import errors", "[cmake][add_partition_import]") {
    const TempDir root;

    // Missing file.
    REQUIRE_FALSE(cup::scaffold::add_partition_import(root.path().string(),
                                                       (root.path() / "nope").string(), "p")
                      .has_value());

    // No module declaration.
    const std::string primary = write_file(root, "bad.cppm", "int x;\n");
    REQUIRE_FALSE(
        cup::scaffold::add_partition_import(root.path().string(), primary, "p").has_value());
}

TEST_CASE("list_subdirs", "[cmake][list_subdirs]") {
    const TempDir root;
    for (const auto* d : {"zeta", "alpha", "mid"}) {
        std::filesystem::create_directories(root.path() / d);
    }
    root.write("file.txt", "x"); // a regular file must be ignored

    REQUIRE(cup::scaffold::list_subdirs(root.path().string()) ==
            std::vector<std::string>{"alpha", "mid", "zeta"});

    // Missing directory returns empty.
    REQUIRE(cup::scaffold::list_subdirs((root.path() / "nope").string()).empty());
}
