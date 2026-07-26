module;
// Declarations only; the definitions are in Cmake.cpp, which is also where <regex>
// stays. See the note at the top of scaffold.cppm.
#include <expected>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
export module cup.scaffold:cmake;

// Re-exported: cup::error::Error is the E of every result below.
export import cup.error;

export namespace cup::scaffold {

// LineMatcher decides whether a line should be removed. It is handed the line
// already trimmed of surrounding whitespace, which is what the Go implementation
// feeds its *regexp.Regexp.
//
// A predicate rather than a compiled pattern: <regex> is a heavy header, and a
// partition interface that named std::regex would have to carry it into every
// consumer's build. Call sites keep their own pattern and hand a lambda over it —
// they are the ones that know whether a regex is even the right tool.
// (Go: RemoveMatchingLine's pattern parameter.)
using LineMatcher = std::function<bool(std::string_view)>;

// read_file_lines returns a file's lines with the trailing newline dropped, so
// there is no phantom empty final line. nullopt means the file could not be read;
// an empty file reads as no lines at all. (Go: ReadFileLines, whose bool this
// optional replaces.)
[[nodiscard]] std::optional<std::vector<std::string>> read_file_lines(
    const std::filesystem::path& path);

// remove_dir deletes a directory tree, ignoring a missing path.
void remove_dir(const std::filesystem::path& path);

// ensure_line appends line to a CMakeLists (creating it if absent) unless it is
// already present, verbatim.
[[nodiscard]] std::expected<void, error::Error> ensure_line(const std::filesystem::path& root,
                                                            const std::filesystem::path& path,
                                                            std::string_view line);

// ensure_line_before inserts line immediately before the first line equal to
// anchor, keeping ordering-sensitive directives correct — third_party must precede
// src/libs. It falls back to appending if the anchor is absent.
[[nodiscard]] std::expected<void, error::Error> ensure_line_before(
    const std::filesystem::path& root, const std::filesystem::path& path, std::string_view line,
    std::string_view anchor);

// remove_line deletes every occurrence of the exact line, reporting whether any was
// removed.
[[nodiscard]] std::expected<bool, error::Error> remove_line(const std::filesystem::path& root,
                                                            const std::filesystem::path& path,
                                                            std::string_view line);

// remove_matching_line deletes every line the matcher accepts, reporting whether
// any was removed. Used where the registered line may carry extra arguments.
[[nodiscard]] std::expected<bool, error::Error> remove_matching_line(
    const std::filesystem::path& root, const std::filesystem::path& path,
    const LineMatcher& matches);

// append_block appends a multi-line block unless marker already appears in the
// file, treating a repeat registration as a no-op.
[[nodiscard]] std::expected<void, error::Error> append_block(const std::filesystem::path& root,
                                                             const std::filesystem::path& path,
                                                             std::string_view marker,
                                                             std::string_view block);

// remove_fetch_content_block removes the FetchContent_Declare(name …) …
// FetchContent_MakeAvailable(name) block, the inverse of a cmake-download
// registration. It reports whether a block was removed.
[[nodiscard]] std::expected<bool, error::Error> remove_fetch_content_block(
    const std::filesystem::path& root, const std::filesystem::path& path, std::string_view name);

// add_module_source appends filename to a library's FILE_SET CXX_MODULES FILES
// block, matching the indentation of the first entry. Idempotent.
[[nodiscard]] std::expected<void, error::Error> add_module_source(
    const std::filesystem::path& root, const std::filesystem::path& path,
    std::string_view filename);

// add_header_source appends filename to a header library's FILE_SET HEADERS FILES
// block, matching the indentation of the first entry. Idempotent. It is the
// header-family analogue of add_module_source.
[[nodiscard]] std::expected<void, error::Error> add_header_source(
    const std::filesystem::path& root, const std::filesystem::path& path,
    std::string_view filename);

// ensure_header_lib_static promotes a header library's CMakeLists from an INTERFACE
// library to a STATIC one so it can compile a .cpp source. A header-only lib is
// declared `add_library(<name> INTERFACE)` with INTERFACE-scoped properties; adding
// the first compiled component turns it into `add_library(<name> STATIC)` with
// those properties rescoped to PUBLIC. A no-op if the lib is already STATIC.
[[nodiscard]] std::expected<void, error::Error> ensure_header_lib_static(
    const std::filesystem::path& root, const std::filesystem::path& path, std::string_view name);

// add_partition_import re-exports a module partition from its lib's primary module
// interface unit, inserting `export import :<partition>;` — appended to the existing
// block of partition imports, or as a fresh block right after the `export module`
// declaration. Idempotent.
[[nodiscard]] std::expected<void, error::Error> add_partition_import(
    const std::filesystem::path& root, const std::filesystem::path& primary,
    std::string_view partition);

// list_subdirs returns the sorted names of the immediate subdirectories of path.
[[nodiscard]] std::vector<std::string> list_subdirs(const std::filesystem::path& path);

}  // namespace cup::scaffold
