// Implementation unit for cup.scaffold:cmake — the surgical edits cup makes to a
// build file it did not write the whole of. Port of internal/scaffold/cmake.go.
//
// Every function here is idempotent by design: `cup add` and `cup register` are run
// repeatedly against the same tree, and re-registering a dependency or re-adding a
// source must leave the file exactly as it was.
module;
#include <algorithm>
#include <cstddef>
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

// read_text slurps a file whole, reporting nullopt if it cannot be read. Binary
// mode so a build file's bytes survive a round trip verbatim.
[[nodiscard]] std::optional<std::string> read_text(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// write_text replaces a file's contents.
[[nodiscard]] std::expected<void, utils::error::Error> write_text(const std::filesystem::path& path,
                                                                  std::string_view content) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << content;
    if (!out) {
        const std::string where = path.string();
        return std::unexpected(
            utils::error::Error(std::format("writing {}", where)));
    }
    return {};
}

// write_lines writes lines each followed by a newline; no lines at all writes an
// empty file rather than a lone newline. (Go: writeLines.)
[[nodiscard]] std::expected<void, utils::error::Error> write_lines(
    const std::filesystem::path& path, const std::vector<std::string>& lines) {
    std::string content;
    for (const auto& line : lines) {
        content += line;
        content += '\n';
    }
    return write_text(path, content);
}

// trim strips surrounding ASCII whitespace. Go's strings.TrimSpace also handles
// Unicode spaces; no build file cup edits contains one, and the lines being
// compared are CMake directives.
[[nodiscard]] std::string_view trim(std::string_view text) {
    constexpr std::string_view kSpace = " \t\n\r\f\v";
    const auto first = text.find_first_not_of(kSpace);
    if (first == std::string_view::npos) {
        return {};
    }
    return text.substr(first, text.find_last_not_of(kSpace) - first + 1);
}

// contains_line reports whether any line is exactly text.
[[nodiscard]] bool contains_line(const std::vector<std::string>& lines, std::string_view text) {
    for (const auto& line : lines) {
        if (line == text) {
            return true;
        }
    }
    return false;
}

// quote_meta escapes the ECMAScript metacharacters in text so a dependency name
// with a dot or a plus in it matches literally. (Go: regexp.QuoteMeta.)
//
// Only the characters that are actually special are escaped: ECMAScript restricts
// identity escapes, so a blanket `\` before every non-alphanumeric would itself be
// invalid syntax for something like `\-`.
[[nodiscard]] std::string quote_meta(std::string_view text) {
    constexpr std::string_view kSpecial = R"(.^$|()[]{}*+?\/)";
    std::string out;
    out.reserve(text.size());
    for (const char c : text) {
        if (kSpecial.find(c) != std::string_view::npos) {
            out.push_back('\\');
        }
        out.push_back(c);
    }
    return out;
}

// indent_of returns the leading whitespace of text — the indentation a new entry
// has to match.
[[nodiscard]] std::string_view indent_of(std::string_view text) {
    const auto first = text.find_first_not_of(" \t");
    return text.substr(0, first == std::string_view::npos ? text.size() : first);
}

// add_to_file_set is the shared body of add_module_source and add_header_source:
// find the FILES block, append filename at the indentation of its first entry, and
// do nothing if it is already listed. Only the pattern and the message differ.
[[nodiscard]] std::expected<void, utils::error::Error> add_to_file_set(
    const std::filesystem::path& root, const std::filesystem::path& path, const std::regex& block,
    std::string_view what, std::string_view filename) {
    const std::string relative = detail::rel(root, path);
    auto content = read_text(path);
    if (!content.has_value()) {
        return std::unexpected(
            utils::error::Error(std::format("cannot update {}: not found", relative)));
    }

    std::smatch found;
    if (!std::regex_search(*content, found, block)) {
        return std::unexpected(
            utils::error::Error(std::format("cannot find {} block in {}", what, relative)));
    }

    // Submatch 2 is the run of already-listed files; submatch 1 is the FILES header
    // above it, which stays put.
    const std::string files = found[2].str();
    const auto at = static_cast<std::size_t>(found.position(2));
    const auto length = static_cast<std::size_t>(found.length(2));

    std::string_view rest = files;
    while (!rest.empty()) {
        const std::size_t end = rest.find('\n');
        if (trim(rest.substr(0, end)) == filename) {
            return {};  // already listed
        }
        if (end == std::string_view::npos) {
            break;
        }
        rest.remove_prefix(end + 1);
    }

    std::string updated = content->substr(0, at);
    updated += files;
    updated += indent_of(files);
    updated += filename;
    updated += '\n';
    updated += content->substr(at + length);

    return write_text(path, updated).transform([&relative, filename] {
        ui::updated(std::format("{}  (+ {})", relative, filename));
    });
}

}  // namespace

std::optional<std::vector<std::string>> read_file_lines(const std::filesystem::path& path) {
    auto text = read_text(path);
    if (!text.has_value()) {
        return std::nullopt;
    }
    if (text->empty()) {
        return std::vector<std::string>{};
    }

    // The trailing newline goes before the split so a well-formed file does not end
    // in a phantom empty line — but a file of nothing *but* newlines still yields
    // one empty line, which is what strings.Split of an empty string does.
    std::string_view body = *text;
    while (body.ends_with('\n')) {
        body.remove_suffix(1);
    }

    std::vector<std::string> lines;
    while (true) {
        const std::size_t end = body.find('\n');
        if (end == std::string_view::npos) {
            lines.emplace_back(body);
            return lines;
        }
        lines.emplace_back(body.substr(0, end));
        body.remove_prefix(end + 1);
    }
}

void remove_dir(const std::filesystem::path& path) {
    std::error_code ec;
    std::filesystem::remove_all(path, ec);  // a missing path is not a failure
}

std::expected<void, utils::error::Error> ensure_line(const std::filesystem::path& root,
                                                     const std::filesystem::path& path,
                                                     std::string_view line) {
    auto lines = read_file_lines(path).value_or(std::vector<std::string>{});
    if (contains_line(lines, line)) {
        return {};
    }
    lines.emplace_back(line);
    return write_lines(path, lines).transform([&root, &path, line] {
        ui::updated(std::format("{}  (+ {})", detail::rel(root, path), line));
    });
}

std::expected<void, utils::error::Error> ensure_line_before(const std::filesystem::path& root,
                                                            const std::filesystem::path& path,
                                                            std::string_view line,
                                                            std::string_view anchor) {
    const auto lines = read_file_lines(path).value_or(std::vector<std::string>{});
    if (contains_line(lines, line)) {
        return {};
    }

    std::vector<std::string> out;
    out.reserve(lines.size() + 1);
    bool inserted = false;
    for (const auto& existing : lines) {
        // The anchor is matched on the trimmed line — an indented add_subdirectory
        // still anchors — while the duplicate check above is exact.
        if (!inserted && trim(existing) == anchor) {
            out.emplace_back(line);
            inserted = true;
        }
        out.push_back(existing);
    }
    if (!inserted) {
        out.emplace_back(line);
    }

    return write_lines(path, out).transform([&root, &path, line] {
        ui::updated(std::format("{}  (+ {})", detail::rel(root, path), line));
    });
}

std::expected<bool, utils::error::Error> remove_line(const std::filesystem::path& root,
                                                     const std::filesystem::path& path,
                                                     std::string_view line) {
    const auto lines = read_file_lines(path);
    if (!lines.has_value()) {
        return false;  // a file that is not there has nothing to remove
    }

    std::vector<std::string> kept;
    kept.reserve(lines->size());
    for (const auto& existing : *lines) {
        if (existing != line) {
            kept.push_back(existing);
        }
    }
    if (kept.size() == lines->size()) {
        return false;
    }

    return write_lines(path, kept).transform([&root, &path, line] {
        ui::removed(std::format("{}  (- {})", detail::rel(root, path), line));
        return true;
    });
}

std::expected<bool, utils::error::Error> remove_matching_line(const std::filesystem::path& root,
                                                              const std::filesystem::path& path,
                                                              const LineMatcher& matches) {
    const auto lines = read_file_lines(path);
    if (!lines.has_value()) {
        return false;
    }

    std::vector<std::string> kept;
    kept.reserve(lines->size());
    for (const auto& existing : *lines) {
        if (!matches(trim(existing))) {
            kept.push_back(existing);
        }
    }
    if (kept.size() == lines->size()) {
        return false;
    }

    return write_lines(path, kept).transform([&root, &path] {
        ui::removed(detail::rel(root, path));
        return true;
    });
}

std::expected<void, utils::error::Error> append_block(
    const std::filesystem::path& root, const std::filesystem::path& path, std::string_view marker,
    std::string_view block) {
    const std::string relative = detail::rel(root, path);
    const std::string existing = read_text(path).value_or(std::string{});
    if (existing.find(marker) != std::string::npos) {
        ui::skipped(std::format("{} already declares {}", relative, marker));
        return {};
    }

    // A blank line separates the block from whatever was there — but only when
    // there *was* something, so a new file starts with the block itself.
    std::string prefix = existing;
    if (!prefix.empty() && !prefix.ends_with('\n')) {
        prefix += '\n';
    }
    if (!prefix.empty() && !prefix.ends_with("\n\n")) {
        prefix += '\n';
    }
    prefix += block;

    return write_text(path, prefix).transform([&relative, marker] {
        ui::updated(std::format("{}  (+ {})", relative, marker));
    });
}

std::expected<bool, utils::error::Error> remove_fetch_content_block(
    const std::filesystem::path& root, const std::filesystem::path& path, std::string_view name) {
    const auto content = read_text(path);
    if (!content.has_value()) {
        return false;
    }

    // [\s\S]*? is ECMAScript's way of spelling Go's (?s:.*?) — any run, newlines
    // included, as short as possible, so two registrations cannot be swallowed as
    // one block.
    const std::string quoted = quote_meta(name);
    const std::regex block(R"(\n*FetchContent_Declare\(\s*\n\s*)" + quoted +
                           R"(\b[\s\S]*?FetchContent_MakeAvailable\(\s*)" + quoted +
                           R"(\s*\)[ \t]*\n?)");
    if (!std::regex_search(*content, block)) {
        return false;
    }

    const std::string updated = std::regex_replace(*content, block, "\n");
    return write_text(path, updated).transform([&root, &path, name] {
        ui::removed(std::format("{}  (- {})", detail::rel(root, path), name));
        return true;
    });
}

std::expected<void, utils::error::Error> add_module_source(const std::filesystem::path& root,
                                                           const std::filesystem::path& path,
                                                           std::string_view filename) {
    static const std::regex kBlock(R"((FILE_SET\s+CXX_MODULES\s+FILES\n)((?:[ \t]+[^\n]+\n)+))");
    return add_to_file_set(root, path, kBlock, "FILE_SET CXX_MODULES", filename);
}

std::expected<void, utils::error::Error> add_header_source(const std::filesystem::path& root,
                                                           const std::filesystem::path& path,
                                                           std::string_view filename) {
    // Unlike the modules form, BASE_DIRS may sit between HEADERS and FILES, so the
    // pattern skips whatever is in between.
    static const std::regex kBlock(R"((FILE_SET\s+HEADERS[\s\S]*?FILES\n)((?:[ \t]+[^\n]+\n)+))");
    return add_to_file_set(root, path, kBlock, "FILE_SET HEADERS", filename);
}

std::expected<void, utils::error::Error> ensure_header_lib_static(const std::filesystem::path& root,
                                                                  const std::filesystem::path& path,
                                                                  std::string_view name) {
    const std::string relative = detail::rel(root, path);
    auto content = read_text(path);
    if (!content.has_value()) {
        return std::unexpected(
            utils::error::Error(std::format("cannot update {}: not found", relative)));
    }

    const std::string interface_decl = std::format("add_library({} INTERFACE)", name);
    const std::size_t at = content->find(interface_decl);
    if (at == std::string::npos) {
        return {};  // already STATIC, or not a header lib cup manages
    }

    content->replace(at, interface_decl.size(), std::format("add_library({} STATIC)", name));
    // Every remaining INTERFACE is a property scope, and a STATIC library's
    // properties have to be PUBLIC instead. \b so a target named INTERFACEs or a
    // path containing the word is left alone.
    static const std::regex kInterfaceKeyword(R"(\bINTERFACE\b)");
    const std::string updated = std::regex_replace(*content, kInterfaceKeyword, "PUBLIC");

    return write_text(path, updated).transform(
        [&relative] { ui::updated(std::format("{}  (INTERFACE -> STATIC)", relative)); });
}

std::expected<void, utils::error::Error> add_partition_import(const std::filesystem::path& root,
                                                              const std::filesystem::path& primary,
                                                              std::string_view partition) {
    const std::string relative = detail::rel(root, primary);
    const auto lines = read_file_lines(primary);
    if (!lines.has_value()) {
        return std::unexpected(
            utils::error::Error(std::format("cannot update {}: not found", relative)));
    }

    const std::string directive = std::format("export import :{};", partition);
    if (contains_line(*lines, directive)) {
        return {};
    }

    // A new import joins the existing block if there is one, so the directives stay
    // together in the order they were added.
    std::size_t after = 0;
    bool found_imports = false;
    for (std::size_t i = 0; i < lines->size(); ++i) {
        if ((*lines)[i].starts_with("export import :")) {
            after = i + 1;
            found_imports = true;
        }
    }

    std::vector<std::string> out;
    if (found_imports) {
        out.assign(lines->begin(), lines->begin() + static_cast<std::ptrdiff_t>(after));
        out.push_back(directive);
        out.insert(out.end(), lines->begin() + static_cast<std::ptrdiff_t>(after), lines->end());
    } else {
        // Otherwise it starts one, separated from the module declaration by a blank
        // line — the shape `cup add lib` scaffolds.
        std::size_t declaration = lines->size();
        for (std::size_t i = 0; i < lines->size(); ++i) {
            if ((*lines)[i].starts_with("export module ")) {
                declaration = i;
                break;
            }
        }
        if (declaration == lines->size()) {
            return std::unexpected(
                utils::error::Error(std::format("cannot find module declaration in {}", relative)));
        }
        out.assign(lines->begin(), lines->begin() + static_cast<std::ptrdiff_t>(declaration) + 1);
        out.emplace_back("");
        out.push_back(directive);
        out.insert(out.end(), lines->begin() + static_cast<std::ptrdiff_t>(declaration) + 1,
                   lines->end());
    }

    return write_lines(primary, out).transform([&relative, &directive] {
        ui::updated(std::format("{}  (+ {})", relative, directive));
    });
}

std::vector<std::string> list_subdirs(const std::filesystem::path& path) {
    // The error_code overloads throughout: a missing or unreadable directory
    // contributes nothing, which is what Go's discarded os.ReadDir error amounts to.
    std::error_code ec;
    std::vector<std::string> dirs;
    for (const auto& entry : std::filesystem::directory_iterator(path, ec)) {
        if (entry.is_directory(ec)) {
            dirs.emplace_back(entry.path().filename().string());
        }
    }
    std::ranges::sort(dirs);
    return dirs;
}

}  // namespace cup::scaffold
