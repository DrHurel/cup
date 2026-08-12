module;
#include <expected>
#include <string>
#include <string_view>
#include <vector>
export module cup.scaffold:naming;

export import cup.error;

export namespace cup::scaffold {

namespace detail {

// Splits dir's segments below its shared prefix with src, dropping the leading
// top-level segment (apps / libs / tests). A lib at src/libs/utils yields
// ["utils"]; a nested src/libs/utils/json yields ["utils", "json"]. Plain string
// splitting, not std::filesystem::path: cup.scaffold reserves <filesystem> for
// the one partition that actually touches disk (see :cmake / :render), and this
// mirrors the Go source, which treats paths as strings too (path/filepath.Rel).
[[nodiscard]] std::vector<std::string> rel_parts(std::string_view src, std::string_view dir) {
    if (!dir.starts_with(src)) {
        return {};
    }
    std::string_view rest = dir.substr(src.size());
    while (rest.starts_with('/')) {
        rest.remove_prefix(1);
    }
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (true) {
        const auto slash = rest.find('/', start);
        if (slash == std::string_view::npos) {
            parts.emplace_back(rest.substr(start));
            break;
        }
        parts.emplace_back(rest.substr(start, slash - start));
        start = slash + 1;
    }
    if (parts.size() <= 1) {
        return {};
    }
    return std::vector<std::string>(parts.begin() + 1, parts.end());
}

[[nodiscard]] std::string join_parts(const std::vector<std::string>& parts, std::string_view sep) {
    std::string out;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i != 0) {
            out += sep;
        }
        for (const char c : parts[i]) {
            out += (c == '-') ? '_' : c;
        }
    }
    return out;
}

[[nodiscard]] constexpr bool is_ident_start(char c) {
    return c == '_' || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

[[nodiscard]] constexpr bool is_ident_char(char c) {
    return is_ident_start(c) || (c >= '0' && c <= '9');
}

}

// validate_ident returns an error unless s is a legal C++ identifier. Its
// signature matches cup::ui::Validator so it can be passed directly to
// cup::ui::text().
[[nodiscard]] std::expected<void, error::Error> validate_ident(std::string_view s) {
    if (s.empty() || !detail::is_ident_start(s.front())) {
        return std::unexpected(error::Error("must be a valid C++ identifier"));
    }
    for (const char c : s.substr(1)) {
        if (!detail::is_ident_char(c)) {
            return std::unexpected(error::Error("must be a valid C++ identifier"));
        }
    }
    return {};
}

// validate_non_empty returns an error if s is blank.
[[nodiscard]] std::expected<void, error::Error> validate_non_empty(std::string_view s) {
    constexpr std::string_view kSpace = " \t\n\r\f\v";
    if (s.find_first_not_of(kSpace) == std::string_view::npos) {
        return std::unexpected(error::Error("must not be empty"));
    }
    return {};
}

// capitalize upper-cases the first byte, leaving the rest untouched — the
// default symbol name derived from a lib/file name (mylib -> Mylib).
[[nodiscard]] std::string capitalize(std::string_view s) {
    if (s.empty()) {
        return std::string(s);
    }
    std::string out(s);
    if (out[0] >= 'a' && out[0] <= 'z') {
        out[0] = static_cast<char>(out[0] - 'a' + 'A');
    }
    return out;
}

// path_to_namespace derives a C++ namespace from a folder under src/, joining
// the path segments (below apps/libs/tests) with "::" and turning hyphens into
// underscores. src/libs/utils/json -> "utils::json".
[[nodiscard]] std::string path_to_namespace(std::string_view src, std::string_view dir) {
    return detail::join_parts(detail::rel_parts(src, dir), "::");
}

// path_to_module mirrors path_to_namespace but joins with "." — the
// module-name separator. src/libs/utils/json -> "utils.json".
[[nodiscard]] std::string path_to_module(std::string_view src, std::string_view dir) {
    return detail::join_parts(detail::rel_parts(src, dir), ".");
}

}
