module;
#include <algorithm>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <ios>
#include <iterator>
#include <optional>
#include <regex>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>
module cup.scaffold;

import cup.ui;

namespace cup::scaffold {
namespace {

// split mirrors Go's strings.Split(s, sep) for a single-byte separator,
// including its two easy-to-miss edge cases: an empty s yields one empty
// element (not zero), and a trailing sep yields a trailing empty element.
std::vector<std::string> split(std::string_view s, char sep) {
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (true) {
        const auto pos = s.find(sep, start);
        if (pos == std::string_view::npos) {
            parts.emplace_back(s.substr(start));
            break;
        }
        parts.emplace_back(s.substr(start, pos - start));
        start = pos + 1;
    }
    return parts;
}

std::string trim(std::string_view s) {
    constexpr std::string_view kSpace = " \t\n\r\f\v";
    const auto first = s.find_first_not_of(kSpace);
    if (first == std::string_view::npos) {
        return {};
    }
    const auto last = s.find_last_not_of(kSpace);
    return std::string(s.substr(first, last - first + 1));
}

std::optional<std::string> slurp(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

std::expected<void, error::Error> write_bytes(const std::filesystem::path& path,
                                              std::string_view content) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << content;
    if (!out) {
        return std::unexpected(error::Error(std::format("writing {}", path.string())));
    }
    return {};
}

std::optional<std::vector<std::string>> read_lines(const std::filesystem::path& path) {
    auto text = slurp(path);
    if (!text.has_value()) {
        return std::nullopt;
    }
    if (text->empty()) {
        return std::vector<std::string>{};
    }
    while (!text->empty() && text->back() == '\n') {
        text->pop_back();
    }
    return split(*text, '\n');
}

std::expected<void, error::Error> write_lines(const std::filesystem::path& path,
                                              const std::vector<std::string>& lines) {
    if (lines.empty()) {
        return write_bytes(path, "");
    }
    std::string content;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (i != 0) {
            content += '\n';
        }
        content += lines[i];
    }
    content += '\n';
    return write_bytes(path, content);
}

std::string quote_meta(std::string_view s) {
    constexpr std::string_view kSpecial = R"(\^$.|?*+()[]{})";
    std::string out;
    out.reserve(s.size());
    for (const char c : s) {
        if (kSpecial.contains(c)) {
            out += '\\';
        }
        out += c;
    }
    return out;
}

std::pair<std::string, int> replace_count(const std::regex& re, const std::string& src,
                                          const std::string& repl) {
    const auto n = static_cast<int>(
        std::distance(std::sregex_iterator(src.begin(), src.end(), re), std::sregex_iterator()));
    if (n == 0) {
        return {src, 0};
    }
    return {std::regex_replace(src, re, repl), n};
}

// Shared by add_module_source / add_header_source: both look for a two-group
// pattern (the FILE_SET header, then the indented block of file entries),
// insert filename into the second group at the first entry's indentation, and
// leave everything else untouched.
std::expected<void, error::Error> add_file_set_entry(const std::filesystem::path& root, const std::filesystem::path& path,
                                                      std::string_view filename,
                                                      const std::regex& pattern,
                                                      std::string_view block_label) {
    auto content = slurp(path);
    if (!content.has_value()) {
        return std::unexpected(error::Error(std::format("cannot update {}: not found", rel(root, path))));
    }
    std::smatch match;
    if (!std::regex_search(*content, match, pattern)) {
        return std::unexpected(
            error::Error(std::format("cannot find {} block in {}", block_label, rel(root, path))));
    }
    const std::string files_block = match[2].str();
    for (const auto& l : split(files_block, '\n')) {
        if (trim(l) == filename) {
            return {}; // already listed
        }
    }
    const std::string first = files_block.substr(0, files_block.find('\n'));
    const auto indent_end = first.find_first_not_of(" \t");
    const std::string indent = first.substr(0, indent_end == std::string::npos ? first.size() : indent_end);
    const std::string new_block = files_block + indent + std::string(filename) + "\n";
    const auto group2_start = static_cast<std::size_t>(match.position(2));
    const auto group2_end = group2_start + static_cast<std::size_t>(match.length(2));
    const std::string updated = content->substr(0, group2_start) + new_block + content->substr(group2_end);
    if (auto written = write_bytes(path, updated); !written.has_value()) {
        return written;
    }
    ui::updated(std::format("{}  (+ {})", rel(root, path), filename));
    return {};
}

std::optional<std::vector<std::string>> insert_partition_import(std::vector<std::string> lines,
                                                                 std::string_view directive) {
    int last_import = -1;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (lines[i].starts_with("export import :")) {
            last_import = static_cast<int>(i);
        }
    }
    if (last_import >= 0) {
        lines.insert(lines.begin() + last_import + 1, std::string(directive));
        return lines;
    }
    if (const auto module_decl = std::ranges::find_if(
            lines, [](const std::string& l) { return l.starts_with("export module "); });
        module_decl != lines.end()) {
        lines.insert(module_decl + 1, {std::string(), std::string(directive)});
        return lines;
    }
    return std::nullopt;
}

std::string join_lines(const std::vector<std::string>& lines) {
    std::string out;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (i != 0) {
            out += '\n';
        }
        out += lines[i];
    }
    out += '\n';
    return out;
}

}

std::optional<std::vector<std::string>> read_file_lines(const std::filesystem::path& path) {
    return read_lines(path);
}

void remove_dir(const std::filesystem::path& path) {
    std::error_code ec;
    std::filesystem::remove_all(path, ec);
}

std::expected<void, error::Error> ensure_line(const std::filesystem::path& root, const std::filesystem::path& path,
                                              std::string_view line) {
    auto lines = read_lines(path).value_or(std::vector<std::string>{});
    for (const auto& l : lines) {
        if (l == line) {
            return {};
        }
    }
    lines.emplace_back(line);
    if (auto written = write_lines(path, lines); !written.has_value()) {
        return written;
    }
    ui::updated(std::format("{}  (+ {})", rel(root, path), line));
    return {};
}

std::expected<void, error::Error> ensure_line_before(const std::filesystem::path& root, const std::filesystem::path& path,
                                                      std::string_view line, std::string_view anchor) {
    auto lines = read_lines(path).value_or(std::vector<std::string>{});
    for (const auto& l : lines) {
        if (l == line) {
            return {};
        }
    }
    std::vector<std::string> out;
    out.reserve(lines.size() + 1);
    bool inserted = false;
    for (const auto& l : lines) {
        if (!inserted && trim(l) == anchor) {
            out.emplace_back(line);
            inserted = true;
        }
        out.push_back(l);
    }
    if (!inserted) {
        out.emplace_back(line);
    }
    if (auto written = write_lines(path, out); !written.has_value()) {
        return written;
    }
    ui::updated(std::format("{}  (+ {})", rel(root, path), line));
    return {};
}

std::expected<bool, error::Error> remove_line(const std::filesystem::path& root, const std::filesystem::path& path,
                                              std::string_view line) {
    auto maybe_lines = read_lines(path);
    if (!maybe_lines.has_value()) {
        return false;
    }
    std::vector<std::string> kept;
    kept.reserve(maybe_lines->size());
    for (const auto& l : *maybe_lines) {
        if (l != line) {
            kept.push_back(l);
        }
    }
    if (kept.size() == maybe_lines->size()) {
        return false;
    }
    if (auto written = write_lines(path, kept); !written.has_value()) {
        return std::unexpected(std::move(written).error());
    }
    ui::removed(std::format("{}  (- {})", rel(root, path), line));
    return true;
}

std::expected<bool, error::Error> remove_matching_line(const std::filesystem::path& root, const std::filesystem::path& path,
                                                        std::string_view pattern) {
    auto maybe_lines = read_lines(path);
    if (!maybe_lines.has_value()) {
        return false;
    }
    const std::regex re{std::string(pattern)};
    std::vector<std::string> kept;
    kept.reserve(maybe_lines->size());
    for (const auto& l : *maybe_lines) {
        if (!std::regex_search(trim(l), re)) {
            kept.push_back(l);
        }
    }
    if (kept.size() == maybe_lines->size()) {
        return false;
    }
    if (auto written = write_lines(path, kept); !written.has_value()) {
        return std::unexpected(std::move(written).error());
    }
    ui::removed(rel(root, path));
    return true;
}

std::expected<void, error::Error> append_block(const std::filesystem::path& root, const std::filesystem::path& path,
                                               std::string_view marker, std::string_view block) {
    const std::string existing = slurp(path).value_or(std::string());
    if (existing.contains(marker)) {
        ui::skipped(std::format("{} already declares {}", rel(root, path), marker));
        return {};
    }
    std::string prefix = existing;
    if (!prefix.empty() && !prefix.ends_with('\n')) {
        prefix += '\n';
    }
    if (!prefix.empty() && !prefix.ends_with("\n\n")) {
        prefix += '\n';
    }
    if (auto written = write_bytes(path, prefix + std::string(block)); !written.has_value()) {
        return written;
    }
    ui::updated(std::format("{}  (+ {})", rel(root, path), marker));
    return {};
}

std::expected<bool, error::Error> remove_fetch_content_block(const std::filesystem::path& root,
                                                              const std::filesystem::path& path,
                                                              std::string_view name) {
    auto content = slurp(path);
    if (!content.has_value()) {
        return false;
    }
    const std::string pattern = "\\n*FetchContent_Declare\\(\\s*\\n\\s*" + quote_meta(name) +
                                "\\b[\\s\\S]*?FetchContent_MakeAvailable\\(\\s*" + quote_meta(name) +
                                "\\s*\\)[ \\t]*\\n?";
    const std::regex re(pattern);
    auto [new_content, n] = replace_count(re, *content, "\n");
    if (n == 0) {
        return false;
    }
    if (auto written = write_bytes(path, new_content); !written.has_value()) {
        return std::unexpected(std::move(written).error());
    }
    ui::removed(std::format("{}  (- {})", rel(root, path), name));
    return true;
}

std::expected<void, error::Error> add_module_source(const std::filesystem::path& root, const std::filesystem::path& path,
                                                     std::string_view filename) {
    static const std::regex pattern(R"((FILE_SET\s+CXX_MODULES\s+FILES\n)((?:[ \t]+[^\n]+\n)+))");
    return add_file_set_entry(root, path, filename, pattern, "FILE_SET CXX_MODULES");
}

std::expected<void, error::Error> add_header_source(const std::filesystem::path& root, const std::filesystem::path& path,
                                                     std::string_view filename) {
    static const std::regex pattern(R"((FILE_SET\s+HEADERS[\s\S]*?FILES\n)((?:[ \t]+[^\n]+\n)+))");
    return add_file_set_entry(root, path, filename, pattern, "FILE_SET HEADERS");
}

std::expected<void, error::Error> ensure_header_lib_static(const std::filesystem::path& root, const std::filesystem::path& path,
                                                            std::string_view name) {
    auto content = slurp(path);
    if (!content.has_value()) {
        return std::unexpected(error::Error(std::format("cannot update {}: not found", rel(root, path))));
    }
    const std::string interface_decl = std::format("add_library({} INTERFACE)", name);
    const auto pos = content->find(interface_decl);
    if (pos == std::string::npos) {
        return {}; // already STATIC (or not a header lib we manage)
    }
    content->replace(pos, interface_decl.size(), std::format("add_library({} STATIC)", name));
    static const std::regex interface_keyword(R"(\bINTERFACE\b)");
    const std::string updated = std::regex_replace(*content, interface_keyword, "PUBLIC");
    if (auto written = write_bytes(path, updated); !written.has_value()) {
        return written;
    }
    ui::updated(std::format("{}  (INTERFACE -> STATIC)", rel(root, path)));
    return {};
}

std::expected<void, error::Error> add_partition_import(const std::filesystem::path& root, const std::filesystem::path& primary,
                                                        std::string_view partition) {
    auto text = slurp(primary);
    if (!text.has_value()) {
        return std::unexpected(error::Error(std::format("cannot update {}: not found", rel(root, primary))));
    }
    while (!text->empty() && text->back() == '\n') {
        text->pop_back();
    }
    const std::string directive = std::format("export import :{};", partition);
    auto lines = split(*text, '\n');
    for (const auto& l : lines) {
        if (l == directive) {
            return {};
        }
    }
    auto out = insert_partition_import(std::move(lines), directive);
    if (!out.has_value()) {
        return std::unexpected(
            error::Error(std::format("cannot find module declaration in {}", rel(root, primary))));
    }
    if (auto written = write_bytes(primary, join_lines(*out)); !written.has_value()) {
        return written;
    }
    ui::updated(std::format("{}  (+ {})", rel(root, primary), directive));
    return {};
}

std::vector<std::string> list_subdirs(const std::filesystem::path& path) {
    std::error_code ec;
    if (!std::filesystem::is_directory(path, ec)) {
        return {};
    }
    std::vector<std::string> dirs;
    for (const auto& entry : std::filesystem::directory_iterator(path, ec)) {
        if (entry.is_directory()) {
            dirs.push_back(entry.path().filename().string());
        }
    }
    std::ranges::sort(dirs);
    return dirs;
}

}
