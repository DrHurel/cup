module;
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <ios>
#include <iterator>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>
export module cup.tmpl:resolve;

import :corpus;
export import cup.error;

export namespace cup::tmpl {

inline constexpr std::string_view kProjectTemplateDir = ".cup/templates";

namespace detail {

[[nodiscard]] std::filesystem::path override_path(const std::filesystem::path& root,
                                                  std::string_view kind,
                                                  std::string_view name) {
    return root / kProjectTemplateDir / kind / name;
}

[[nodiscard]] std::optional<std::string> read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

[[nodiscard]] std::optional<std::string> local_read(const std::filesystem::path& root,
                                                    std::string_view kind, std::string_view name) {
    return root.empty() ? std::nullopt : read_file(override_path(root, kind, name));
}

[[nodiscard]] std::expected<void, error::Error> write_file(const std::filesystem::path& path,
                                                           std::string_view content) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << content;
    if (!out) {
        return std::unexpected(error::Error(std::format("writing {}", path.string())));
    }
    return {};
}

[[nodiscard]] std::expected<void, error::Error> create_dir(const std::filesystem::path& dst) {
    std::error_code ec;
    std::filesystem::create_directories(dst, ec);
    if (ec) {
        return std::unexpected(
            error::Error(std::format("creating {}: {}", dst.string(), ec.message())));
    }
    return {};
}

}

[[nodiscard]] bool exists(const std::filesystem::path& root, std::string_view family,
                          std::string_view kind, std::string_view name) {
    std::error_code ec;
    return (!root.empty() &&
            std::filesystem::exists(detail::override_path(root, kind, name), ec)) ||
           builtin_exists(family, kind, name);
}

[[nodiscard]] bool is_compiled(const std::filesystem::path& root, std::string_view family,
                               std::string_view kind) {
    return family == "headers" && exists(root, family, kind, "source.h.tmpl") &&
           exists(root, family, kind, "source.cpp.tmpl");
}

[[nodiscard]] std::expected<std::string, error::Error> read(const std::filesystem::path& root,
                                                            std::string_view family,
                                                            std::string_view kind,
                                                            std::string_view name) {
    auto resolved = detail::local_read(root, kind, name).or_else([family, kind, name] {
        return builtin_read(family, kind, name).transform(
            [](std::string_view content) { return std::string(content); });
    });
    return error::require(std::move(resolved),
                          std::format("no such template: {}/{}/{}", family, kind, name));
}

namespace detail {

[[nodiscard]] bool has_component_source(const std::filesystem::path& root,
                                        std::string_view family, std::string_view kind) {
    if (family == "headers") {
        return exists(root, family, kind, "source.hpp.tmpl") || is_compiled(root, family, kind);
    }
    return exists(root, family, kind, "source.cppm.tmpl");
}

}

[[nodiscard]] std::vector<std::string> kinds(const std::filesystem::path& root,
                                             std::string_view family) {
    std::set<std::string, std::less<>> seen;
    const auto add = [&root, family, &seen](const std::string& name) {
        if (name == "app" || name == "test" || name == "project") {
            return;
        }
        if (detail::has_component_source(root, family, name)) {
            seen.insert(name);
        }
    };

    for (const auto& kind : builtin_kinds(family)) {
        add(kind);
    }
    if (!root.empty()) {
        std::error_code ec;
        for (const auto& entry :
             std::filesystem::directory_iterator(root / kProjectTemplateDir, ec)) {
            if (entry.is_directory(ec)) {
                add(entry.path().filename().string());
            }
        }
    }
    return {seen.begin(), seen.end()};
}

namespace detail {

[[nodiscard]] std::expected<std::vector<BuiltinFile>, error::Error> kind_files(
    std::string_view family, std::string_view kind) {
    auto files = builtin_files(family, kind);
    return error::require(files.empty() ? std::nullopt : std::optional(std::move(files)),
                          std::format("no such template kind: {}/{}", family, kind));
}

}

[[nodiscard]] std::expected<void, error::Error> copy_builtin(std::string_view family,
                                                             std::string_view kind,
                                                             const std::filesystem::path& dst) {
    return detail::create_dir(dst)
        .and_then([family, kind] { return detail::kind_files(family, kind); })
        .and_then([&dst](const std::vector<BuiltinFile>& files) {
            return error::for_each(files, [&dst](const BuiltinFile& file) {
                return detail::write_file(dst / file.name, file.content);
            });
        });
}

}
