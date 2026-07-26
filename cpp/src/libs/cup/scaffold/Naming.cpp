// Implementation unit for cup.scaffold:naming — the names cup derives from what a
// user typed and from where a directory sits. Port of internal/scaffold/naming.go.
module;
#include <cctype>
#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>
module cup.scaffold;

namespace cup::scaffold {
namespace {

// Go matches ^[A-Za-z_][A-Za-z0-9_]*$ with a regexp. Hand-rolled here because the
// pattern is three predicates, and because <regex> would then have to be reachable
// from every translation unit that validates a name.
//
// The check is deliberately ASCII-only, and that is not a simplification: a C++
// identifier may contain other characters, but cup's Go implementation rejects them
// (its test pins "é" as invalid) and the two implementations have to agree. Casting
// through unsigned char is what keeps a byte >= 0x80 from reaching std::isalpha as
// a negative int, which is undefined.
[[nodiscard]] bool is_ident_start(char c) {
    const auto byte = static_cast<unsigned char>(c);
    return byte < 0x80 && (std::isalpha(byte) != 0 || c == '_');
}

[[nodiscard]] bool is_ident_char(char c) {
    const auto byte = static_cast<unsigned char>(c);
    return byte < 0x80 && (std::isalnum(byte) != 0 || c == '_');
}

[[nodiscard]] bool is_ident(std::string_view text) {
    if (text.empty() || !is_ident_start(text.front())) {
        return false;
    }
    for (const char c : text.substr(1)) {
        if (!is_ident_char(c)) {
            return false;
        }
    }
    return true;
}

// is_blank reports whether text is empty or all ASCII whitespace, which is what
// strings.TrimSpace(s) == "" amounts to for the names cup accepts.
[[nodiscard]] bool is_blank(std::string_view text) {
    return text.find_first_not_of(" \t\n\r\f\v") == std::string_view::npos;
}

// rel_parts returns the path segments of dir relative to src, dropping the leading
// top-level segment (apps / libs / tests). A lib at src/libs/utils yields
// {"utils"}; a nested src/libs/utils/json yields {"utils", "json"}.
//
// lexically_relative stands in for filepath.Rel: it is purely textual, so an
// unrelated dir yields an empty path exactly as Go's error case yields nil, and
// neither implementation touches the disk to answer.
[[nodiscard]] std::vector<std::string> rel_parts(const std::filesystem::path& src,
                                                 const std::filesystem::path& dir) {
    std::vector<std::string> parts;
    for (const auto& part : dir.lexically_relative(src)) {
        parts.emplace_back(part.string());
    }
    if (parts.size() <= 1) {
        return {};
    }
    return {parts.begin() + 1, parts.end()};
}

// join_parts joins segments with sep, turning hyphens into underscores — a
// directory may be named my-lib, but neither a namespace nor a module name may.
[[nodiscard]] std::string join_parts(const std::vector<std::string>& parts,
                                     std::string_view sep) {
    std::string joined;
    for (const auto& part : parts) {
        if (!joined.empty()) {
            joined += sep;
        }
        for (const char c : part) {
            joined.push_back(c == '-' ? '_' : c);
        }
    }
    return joined;
}

}  // namespace

std::expected<void, error::Error> validate_ident(std::string_view text) {
    if (is_ident(text)) {
        return {};
    }
    return std::unexpected(error::Error("must be a valid C++ identifier"));
}

std::expected<void, error::Error> validate_non_empty(std::string_view text) {
    if (is_blank(text)) {
        return std::unexpected(error::Error("must not be empty"));
    }
    return {};
}

std::string capitalize(std::string_view text) {
    if (text.empty()) {
        return {};
    }
    std::string out(text);
    out.front() = static_cast<char>(std::toupper(static_cast<unsigned char>(out.front())));
    return out;
}

std::string path_to_namespace(const std::filesystem::path& src,
                              const std::filesystem::path& dir) {
    return join_parts(rel_parts(src, dir), "::");
}

std::string path_to_module(const std::filesystem::path& src, const std::filesystem::path& dir) {
    return join_parts(rel_parts(src, dir), ".");
}

}  // namespace cup::scaffold
