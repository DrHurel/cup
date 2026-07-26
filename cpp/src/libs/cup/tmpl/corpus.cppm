module;
// The corpus itself: a generated plain header/TU pair produced by
// cmake/EmbedTemplates.cmake, included here in the global module fragment. Its
// declarations live in cup::templates and stay internal to cup.tmpl — nothing
// below re-exports them, so consumers see only the accessors in this partition.
#include <cup/embedded_templates.hpp>

#include <functional>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>
export module cup.tmpl:corpus;

export namespace cup::tmpl {

namespace detail {

// corpus_path builds a lookup key. The embedded layout mirrors the Go
// implementation's internal/tmpl/files/ exactly, minus that "files/" prefix
// (cmake globs templates/ directly), so a key is just <family>/<kind>/<name>.
[[nodiscard]] std::string corpus_path(std::string_view family, std::string_view kind,
                                      std::string_view name) {
    std::string path;
    path.reserve(family.size() + kind.size() + name.size() + 2);
    path.append(family).append("/").append(kind).append("/").append(name);
    return path;
}

}  // namespace detail

// BuiltinFile is one file of a built-in template kind: its base name and the
// embedded content. (Go: an embed.FS DirEntry paired with its ReadFile bytes.)
struct BuiltinFile {
    std::string name;
    std::string_view content;
};

// builtin_read returns the embedded content of <family>/<kind>/<name>, or nullopt
// when the corpus has no such template. (Go: embedded.ReadFile.)
[[nodiscard]] std::optional<std::string_view> builtin_read(std::string_view family,
                                                           std::string_view kind,
                                                           std::string_view name) {
    return templates::read(detail::corpus_path(family, kind, name));
}

// builtin_exists reports whether the corpus carries <family>/<kind>/<name>.
// (Go: embedded.Open, error checked.)
[[nodiscard]] bool builtin_exists(std::string_view family, std::string_view kind,
                                  std::string_view name) {
    return templates::exists(detail::corpus_path(family, kind, name));
}

// builtin_kinds lists every built-in template directory for family (including the
// special app, test and project dirs), used by `cup template new` to offer a
// starting point to copy.
//
// The corpus is a flat list of file paths, so a "directory" is any first path
// segment under <family>/ that is followed by a separator — the equivalent of
// embed.FS's ReadDir filtered to IsDir. std::set both dedupes and sorts, matching
// the Go implementation's trailing sort.Strings.
[[nodiscard]] std::vector<std::string> builtin_kinds(std::string_view family) {
    const std::string prefix = std::string(family) + "/";
    // std::less<> so the string_view segments below need no intermediate string.
    std::set<std::string, std::less<>> dirs;
    for (const auto& entry : templates::all()) {
        if (!entry.path.starts_with(prefix)) {
            continue;
        }
        const std::string_view rest = entry.path.substr(prefix.size());
        if (const auto slash = rest.find('/'); slash != std::string_view::npos) {
            dirs.emplace(rest.substr(0, slash));
        }
    }
    return {dirs.begin(), dirs.end()};
}

// builtin_files lists the files directly inside built-in kind <family>/<kind>,
// skipping anything nested deeper — the same shape CopyBuiltin walks in Go, where
// nested DirEntries are skipped with e.IsDir().
[[nodiscard]] std::vector<BuiltinFile> builtin_files(std::string_view family,
                                                     std::string_view kind) {
    const std::string prefix = std::string(family) + "/" + std::string(kind) + "/";
    std::vector<BuiltinFile> files;
    for (const auto& entry : templates::all()) {
        if (!entry.path.starts_with(prefix)) {
            continue;
        }
        const std::string_view name = entry.path.substr(prefix.size());
        if (name.contains('/')) {
            continue;  // nested directory, not a file of this kind
        }
        files.emplace_back(std::string(name), entry.content);
    }
    return files;
}

}  // namespace cup::tmpl
