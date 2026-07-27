// Implementation unit for cup.scaffold:render — template substitution and the two
// ways cup puts a rendered file on disk. Port of internal/scaffold/render.go.
module;
#include <cstddef>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <ios>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
module cup.scaffold;

import cup.tmpl;
import cup.ui;

namespace cup::scaffold {
namespace {

// replace_all substitutes every occurrence of needle, which std::string has no
// method for. Written out rather than looped in place because a value may be longer
// than what it replaces, so an in-place walk would have to re-find its position
// after each edit.
[[nodiscard]] std::string replace_all(std::string_view text, std::string_view needle,
                                      std::string_view value) {
    std::string out;
    out.reserve(text.size());
    std::size_t at = 0;
    while (true) {
        const std::size_t found = text.find(needle, at);
        if (found == std::string_view::npos) {
            out.append(text.substr(at));
            return out;
        }
        out.append(text.substr(at, found - at));
        out.append(value);
        at = found + needle.size();
    }
}

// unresolved returns the {{placeholder}}s still standing in text, deduplicated and
// sorted. std::set supplies both, matching Go's map + sort.Strings.
//
// It is the hand-rolled form of Go's `\{\{[^}]+\}\}`: a run with no closing brace
// inside it. "{{a}b}}" therefore matches nothing in either implementation, and so
// does an empty "{{}}".
[[nodiscard]] std::set<std::string> unresolved(std::string_view text) {
    std::set<std::string> found;
    for (std::size_t at = text.find("{{"); at != std::string_view::npos;
         at = text.find("{{", at + 1)) {
        const std::size_t body = at + 2;
        const std::size_t close = text.find('}', body);
        if (close == std::string_view::npos || close == body ||
            !text.substr(close).starts_with("}}")) {
            continue;
        }
        found.emplace(text.substr(at, close + 2 - at));
    }
    return found;
}

// join renders a sorted placeholder set as the error message's list.
[[nodiscard]] std::string join(const std::set<std::string>& items, std::string_view sep) {
    std::string out;
    for (const auto& item : items) {
        if (!out.empty()) {
            out += sep;
        }
        out += item;
    }
    return out;
}

// write_bytes writes content to path verbatim, creating parent directories first.
[[nodiscard]] std::expected<void, utils::error::Error> write_bytes(
    const std::filesystem::path& path, std::string_view content) {
    if (const std::filesystem::path parent = path.parent_path(); !parent.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(parent, ec);
        if (ec) {
            const std::string where = parent.string();
            const std::string why = ec.message();
            return std::unexpected(
                utils::error::Error(std::format("creating {}: {}", where, why)));
        }
    }
    // Binary mode so a template's bytes survive verbatim, truncating whatever was
    // there — the caller has already decided the file may be replaced.
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << content;
    if (!out) {
        const std::string where = path.string();
        return std::unexpected(
            utils::error::Error(std::format("writing {}", where)));
    }
    return {};
}

}  // namespace

namespace detail {

std::string rel(const std::filesystem::path& root, const std::filesystem::path& path) {
    const std::filesystem::path relative = path.lexically_relative(root);
    return relative.empty() ? path.string() : relative.string();
}

}  // namespace detail

std::expected<std::string, utils::error::Error> render(
    const std::filesystem::path& root, std::string_view family, std::string_view kind,
    std::string_view name, const Vars& vars) {
    auto raw = tmpl::read(root, family, kind, name);
    if (!raw.has_value()) {
        // The template layer's own error is dropped, not wrapped: it distinguishes
        // "no override and no built-in" in terms of the corpus, and what a user
        // needs here is the kind and name they asked for. (Go does the same.)
        return std::unexpected(
            utils::error::Error(std::format("template {}/{} not found", kind, name)));
    }

    std::string content = *std::move(raw);
    // Substitution runs to a fixed point: a value may itself contain a placeholder,
    // so one pass per variable is the most that can be needed, and the loop stops as
    // soon as a pass changes nothing.
    for (std::size_t pass = 0; pass <= vars.size(); ++pass) {
        const std::string before = content;
        for (const auto& [key, value] : vars) {
            std::string placeholder = "{{";
            placeholder += key;
            placeholder += "}}";
            content = replace_all(content, placeholder, value);
        }
        if (content == before) {
            break;
        }
    }

    if (const auto left = unresolved(content); !left.empty()) {
        const std::string listed = join(left, ", ");
        return std::unexpected(utils::error::Error(
            std::format("template {}/{} has unresolved placeholders: {}", kind, name, listed)));
    }
    return content;
}

std::expected<bool, utils::error::Error> write_file(const std::filesystem::path& root,
                                                    const std::filesystem::path& path,
                                                    std::string_view content) {
    std::error_code ec;
    if (std::filesystem::exists(path, ec)) {
        const std::string relative = detail::rel(root, path);
        auto overwrite = ui::confirm(std::format("{} exists. overwrite?", relative), false);
        if (!overwrite.has_value()) {
            // Only an abort (Ctrl+D / end of input) gets here, and it is the
            // caller's to report — a declined overwrite is the `false` below.
            return std::unexpected(std::move(overwrite).error());
        }
        if (!*overwrite) {
            ui::skipped(relative);
            return false;
        }
    }
    return write_bytes(path, content).transform([&root, &path] {
        ui::wrote(detail::rel(root, path));
        return true;
    });
}

std::expected<void, utils::error::Error> ensure_file(const std::filesystem::path& root,
                                                     const std::filesystem::path& path,
                                                     std::string_view content) {
    std::error_code ec;
    if (std::filesystem::exists(path, ec)) {
        return {};
    }
    return write_bytes(path, content).transform(
        [&root, &path] { ui::wrote(detail::rel(root, path)); });
}

}  // namespace cup::scaffold
