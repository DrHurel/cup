module;
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <ios>
#include <map>
#include <regex>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>
module cup.scaffold;

import cup.tmpl;
import cup.ui;

namespace cup::scaffold {
namespace {

std::string replace_all(std::string s, std::string_view from, std::string_view to) {
    if (from.empty()) {
        return s;
    }
    std::string out;
    out.reserve(s.size());
    std::size_t pos = 0;
    while (true) {
        const auto found = s.find(from, pos);
        if (found == std::string::npos) {
            out.append(s, pos, std::string::npos);
            break;
        }
        out.append(s, pos, found - pos);
        out.append(to);
        pos = found + from.size();
    }
    return out;
}

const std::regex& placeholder_regex() {
    static const std::regex re(R"(\{\{[^}]+\}\})");
    return re;
}

// Deduplicated, sorted (std::set orders by std::string's operator<, matching
// Go's sort.Strings) so the error message lists each unresolved placeholder
// once, in a stable order.
std::vector<std::string> unresolved_placeholders(const std::string& content) {
    std::set<std::string> unique;
    for (auto it = std::sregex_iterator(content.begin(), content.end(), placeholder_regex());
         it != std::sregex_iterator(); ++it) {
        unique.insert(it->str());
    }
    return {unique.begin(), unique.end()};
}

std::string join(const std::vector<std::string>& items, std::string_view sep) {
    std::string out;
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (i != 0) {
            out += sep;
        }
        out += items[i];
    }
    return out;
}

std::expected<void, error::Error> create_dir(const std::filesystem::path& dir) {
    if (dir.empty()) {
        return {};
    }
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) {
        return std::unexpected(
            error::Error(std::format("creating {}: {}", dir.string(), ec.message())));
    }
    return {};
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

}

std::expected<std::string, error::Error> render(const std::filesystem::path& root,
                                                 std::string_view family, std::string_view kind,
                                                 std::string_view name,
                                                 const std::map<std::string, std::string>& vars) {
    auto raw = tmpl::read(root, family, kind, name);
    if (!raw.has_value()) {
        return std::unexpected(error::Error(std::format("template {}/{} not found", kind, name)));
    }
    std::string content = std::move(*raw);
    for (std::size_t i = 0; i <= vars.size(); ++i) {
        const std::string before = content;
        for (const auto& [k, v] : vars) {
            content = replace_all(std::move(content), "{{" + k + "}}", v);
        }
        if (content == before) {
            break;
        }
    }
    if (auto left = unresolved_placeholders(content); !left.empty()) {
        return std::unexpected(error::Error(std::format(
            "template {}/{} has unresolved placeholders: {}", kind, name, join(left, ", "))));
    }
    return content;
}

std::string rel(const std::filesystem::path& root, const std::filesystem::path& path) {
    const auto relative = path.lexically_relative(root);
    if (relative.empty()) {
        return path.string();
    }
    return relative.string();
}

std::expected<bool, error::Error> write_file(const std::filesystem::path& root,
                                             const std::filesystem::path& path,
                                             std::string_view content) {
    std::error_code ec;
    if (std::filesystem::exists(path, ec)) {
        auto confirmed = ui::confirm(std::format("{} exists. overwrite?", rel(root, path)), false);
        if (!confirmed.has_value()) {
            return std::unexpected(std::move(confirmed).error());
        }
        if (!*confirmed) {
            ui::skipped(rel(root, path));
            return false;
        }
    }
    if (auto created = create_dir(path.parent_path()); !created.has_value()) {
        return std::unexpected(std::move(created).error());
    }
    if (auto written = write_bytes(path, content); !written.has_value()) {
        return std::unexpected(std::move(written).error());
    }
    ui::wrote(rel(root, path));
    return true;
}

std::expected<void, error::Error> ensure_file(const std::filesystem::path& root,
                                              const std::filesystem::path& path,
                                              std::string_view content) {
    std::error_code ec;
    if (std::filesystem::exists(path, ec)) {
        return {};
    }
    if (auto created = create_dir(path.parent_path()); !created.has_value()) {
        return created;
    }
    if (auto written = write_bytes(path, content); !written.has_value()) {
        return written;
    }
    ui::wrote(rel(root, path));
    return {};
}

}
