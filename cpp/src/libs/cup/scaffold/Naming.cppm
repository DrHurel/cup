module;
// Declarations only; the definitions are in Naming.cpp. See the note at the top of
// scaffold.cppm for the GCC 14 constraints that shape every partition here.
#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
export module cup.scaffold:naming;

// Re-exported: cup::error::Error is the E of the validators' results.
export import cup.error;

export namespace cup::scaffold {

// validate_ident fails unless text is a legal C++ identifier. It is the validator
// behind every "class name" / "symbol name" prompt, so its message is written to be
// read by a user mid-prompt, not by a log. (Go: ValidateIdent.)
[[nodiscard]] std::expected<void, error::Error> validate_ident(std::string_view text);

// validate_non_empty fails if text is blank.
[[nodiscard]] std::expected<void, error::Error> validate_non_empty(std::string_view text);

// capitalize upper-cases the first character, leaving the rest untouched — the
// default symbol name derived from a lib or file name (mylib -> Mylib).
[[nodiscard]] std::string capitalize(std::string_view text);

// path_to_namespace derives a C++ namespace from a folder under src/, joining the
// path segments below the top-level apps/libs/tests directory with "::" and turning
// hyphens into underscores. src/libs/utils/json -> "utils::json".
[[nodiscard]] std::string path_to_namespace(const std::filesystem::path& src,
                                            const std::filesystem::path& dir);

// path_to_module mirrors path_to_namespace but joins with "." — the module-name
// separator. src/libs/utils/json -> "utils.json".
[[nodiscard]] std::string path_to_module(const std::filesystem::path& src,
                                         const std::filesystem::path& dir);

}  // namespace cup::scaffold
