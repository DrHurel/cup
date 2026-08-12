module;
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
export module cup.scaffold:cmake;

export import cup.error;

// Declarations only, same reasoning as render.cppm: the definitions (in
// Cmake.cpp) import cup.ui and use <filesystem>/<regex>/<fstream> internally,
// and this is cup.scaffold's one heavy-header partition already (see
// render.cppm's note) — a second interface partition reaching those headers
// would reproduce the cup.project :config/:io BMI-merge failure from
// docs/migration-cpp23.md's constraint 1. Paths are plain strings, not
// std::filesystem::path, so this interface never needs <filesystem> either.
export namespace cup::scaffold {

// read_file_lines exposes a file's lines to callers outside this module (e.g.
// a future dependency scanner). nullopt only if the file could not be read.
[[nodiscard]] std::optional<std::vector<std::string>> read_file_lines(const std::string& path);

// remove_dir deletes a directory tree, ignoring a missing path.
void remove_dir(const std::string& path);

// ensure_line appends line to a CMakeLists (creating it if absent) unless
// already present, verbatim.
[[nodiscard]] std::expected<void, error::Error> ensure_line(const std::string& root,
                                                             const std::string& path,
                                                             std::string_view line);

// ensure_line_before inserts line immediately before the first line equal to
// anchor, keeping ordering-sensitive directives correct (e.g. third_party must
// precede src/libs). Falls back to appending if the anchor is absent.
[[nodiscard]] std::expected<void, error::Error> ensure_line_before(const std::string& root,
                                                                   const std::string& path,
                                                                   std::string_view line,
                                                                   std::string_view anchor);

// remove_line deletes every occurrence of the exact line. Returns true if any
// was removed.
[[nodiscard]] std::expected<bool, error::Error> remove_line(const std::string& root,
                                                             const std::string& path,
                                                             std::string_view line);

// remove_matching_line deletes every line whose trimmed text matches the
// ECMAScript regex pattern at its start. Used where the registered line may
// carry extra arguments.
[[nodiscard]] std::expected<bool, error::Error> remove_matching_line(const std::string& root,
                                                                      const std::string& path,
                                                                      std::string_view pattern);

// append_block appends a multi-line block unless marker already appears in the
// file, treating a repeat registration as a no-op.
[[nodiscard]] std::expected<void, error::Error> append_block(const std::string& root,
                                                              const std::string& path,
                                                              std::string_view marker,
                                                              std::string_view block);

// remove_fetch_content_block removes the FetchContent_Declare(name …) …
// FetchContent_MakeAvailable(name) block, the inverse of a cmake-download
// registration. Returns true if a block was removed.
[[nodiscard]] std::expected<bool, error::Error> remove_fetch_content_block(
    const std::string& root, const std::string& path, std::string_view name);

// add_module_source appends filename to a library's FILE_SET CXX_MODULES
// FILES block, matching the indentation of the first entry. Idempotent.
[[nodiscard]] std::expected<void, error::Error> add_module_source(const std::string& root,
                                                                   const std::string& path,
                                                                   std::string_view filename);

// add_header_source appends filename to a header library's FILE_SET HEADERS
// FILES block, matching the indentation of the first entry. Idempotent. It is
// the header-family analogue of add_module_source.
[[nodiscard]] std::expected<void, error::Error> add_header_source(const std::string& root,
                                                                   const std::string& path,
                                                                   std::string_view filename);

// ensure_header_lib_static promotes a header library's CMakeLists from an
// INTERFACE library to a STATIC one so it can compile a .cpp source. A
// header-only lib is declared `add_library(<name> INTERFACE)` with
// INTERFACE-scoped properties; adding the first compiled component turns it
// into `add_library(<name> STATIC)` with those properties rescoped to PUBLIC.
// A no-op if the lib is already STATIC.
[[nodiscard]] std::expected<void, error::Error> ensure_header_lib_static(const std::string& root,
                                                                         const std::string& path,
                                                                         std::string_view name);

// add_partition_import re-exports a module partition from its lib's primary
// module interface unit, inserting `export import :<partition>;` — appended
// to the existing block of partition imports, or as a fresh block right after
// the `export module` declaration. Idempotent.
[[nodiscard]] std::expected<void, error::Error> add_partition_import(const std::string& root,
                                                                      const std::string& primary,
                                                                      std::string_view partition);

// list_subdirs returns the sorted names of immediate subdirectories of path.
[[nodiscard]] std::vector<std::string> list_subdirs(const std::string& path);

}
