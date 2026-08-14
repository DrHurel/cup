module;
#include <expected>
#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <string_view>
export module cup.scaffold:render;

export import cup.error;

// Declarations only — the definitions (in Render.cpp) import cup.tmpl and
// cup.ui, and combining that with this partition's own heavy includes
// (<filesystem>, <regex>, <format>, <fstream>) in one interface unit ICEs GCC
// 14 (same failure class as docs/migration-cpp23.md's constraint 2: a heavy
// dependency in an interface unit's global module fragment). Render.cpp is a
// module implementation unit — its global module fragment never reaches a
// BMI — the same fix cup.project used for toml++ in Toml.cpp.
export namespace cup::scaffold {

// render loads template <family>/<kind>/<name> (preferring the project's own
// copy under .cup/templates/<kind>/), substitutes every {{key}} from vars, and
// fails if any placeholder was left unresolved. Substitution runs to a fixed
// point, so a variable whose value itself contains a placeholder (e.g. a
// {{hello}} greeting embedding {{name}}) is fully resolved.
[[nodiscard]] std::expected<std::string, error::Error> render(
    const std::filesystem::path& root, std::string_view family, std::string_view kind,
    std::string_view name, const std::map<std::string, std::string, std::less<>>& vars);

// rel renders path relative to root for tidy log output.
[[nodiscard]] std::string rel(const std::filesystem::path& root,
                              const std::filesystem::path& path);

// write_file writes content to path, prompting before overwriting an existing
// file. It reports true if written, false if the user declined the overwrite —
// a legitimate skip, not an error.
[[nodiscard]] std::expected<bool, error::Error> write_file(const std::filesystem::path& root,
                                                            const std::filesystem::path& path,
                                                            std::string_view content);

// ensure_file creates path with content only if it does not already exist.
[[nodiscard]] std::expected<void, error::Error> ensure_file(const std::filesystem::path& root,
                                                             const std::filesystem::path& path,
                                                             std::string_view content);

}
