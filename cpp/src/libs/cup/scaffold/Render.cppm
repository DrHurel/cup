module;
// Declarations only; the definitions are in Render.cpp. See the note at the top of
// scaffold.cppm — render() returning std::expected<std::string, Error> is precisely
// the signature GCC 14 cannot cope with when it is defined inline in an interface.
#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
export module cup.scaffold:render;

import :std;
// Re-exported: cup::error::Error is the E of every result below.
export import cup.error;

export namespace cup::scaffold {

// render loads template <family>/<kind>/<name> (preferring the project's own copy
// under .cup/templates/<kind>/), substitutes every {{key}} from vars, and fails if
// any placeholder was left unresolved.
//
// Substitution runs to a fixed point, so a variable whose value itself contains a
// placeholder — the {{hello}} greeting embeds {{name}} — is fully resolved.
// (Go: Render.)
[[nodiscard]] std::expected<std::string, error::Error> render(const std::filesystem::path& root,
                                                              std::string_view family,
                                                              std::string_view kind,
                                                              std::string_view name,
                                                              const Vars& vars);

// write_file writes content to path, prompting before overwriting an existing file.
// It reports true if written, false if the user declined the overwrite — a
// legitimate skip, not an error. (Go: WriteFile.)
[[nodiscard]] std::expected<bool, error::Error> write_file(const std::filesystem::path& root,
                                                           const std::filesystem::path& path,
                                                           std::string_view content);

// ensure_file creates path with content only if it does not already exist.
[[nodiscard]] std::expected<void, error::Error> ensure_file(const std::filesystem::path& root,
                                                            const std::filesystem::path& path,
                                                            std::string_view content);

namespace detail {

// rel renders path relative to root for tidy log output, falling back to the path
// itself when the two are unrelated.
//
// Go keeps this unexported and shares it across cmake.go, compiler.go and
// render.go, which are one package. Here those are three partitions, so it is
// exported — under detail, which is how cup.tmpl and cup.ui already mark "part of
// the implementation, reachable because the language leaves no better option".
[[nodiscard]] std::string rel(const std::filesystem::path& root,
                              const std::filesystem::path& path);

}  // namespace detail

}  // namespace cup::scaffold
