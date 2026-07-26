module;
#include <expected>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>
export module cup.tmpl:resolve;

import :corpus;
// Re-exported: cup::error::Error is the E of every result below.
export import cup.error;

export namespace cup::tmpl {

// kProjectTemplateDir is the per-project directory that holds template overrides
// and additions, relative to the project root. (Go: ProjectTemplateDir.)
inline constexpr std::string_view kProjectTemplateDir = ".cup/templates";

namespace detail {

// override_path is where a project-local copy of <kind>/<name> would live.
[[nodiscard]] std::filesystem::path override_path(const std::filesystem::path& root,
                                                  std::string_view kind,
                                                  std::string_view name) {
    return root / kProjectTemplateDir / kind / name;
}

// read_file slurps a file whole, reporting nullopt if it cannot be read. Opened in
// binary mode so template bytes survive verbatim.
[[nodiscard]] std::optional<std::string> read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

}  // namespace detail

// exists reports whether template file <kind>/<name> is available for family,
// either as a project-local override or a built-in default. root may be empty to
// consult only the built-ins.
[[nodiscard]] bool exists(const std::filesystem::path& root, std::string_view family,
                          std::string_view kind, std::string_view name) {
    if (!root.empty()) {
        // std::error_code overload: a permission failure mid-walk reports "not
        // there" rather than throwing, matching os.Stat's error return in Go.
        std::error_code ec;
        if (std::filesystem::exists(detail::override_path(root, kind, name), ec)) {
            return true;
        }
    }
    return builtin_exists(family, kind, name);
}

// is_compiled reports whether a headers-family kind scaffolds a compiled component
// — a .h declaration paired with a .cpp definition (source.h.tmpl +
// source.cpp.tmpl) — rather than a single header-only .hpp. Templates (e.g.
// templated-class) stay header-only, so they are not compiled. Module kinds are
// never compiled in this sense.
[[nodiscard]] bool is_compiled(const std::filesystem::path& root, std::string_view family,
                               std::string_view kind) {
    return family == "headers" && exists(root, family, kind, "source.h.tmpl") &&
           exists(root, family, kind, "source.cpp.tmpl");
}

// read returns the bytes of template file <kind>/<name>, preferring a
// project-local copy under <root>/.cup/templates/<kind>/ over the built-in default
// in <family>/<kind>/. root may be empty to consult only the built-in templates.
//
// A local file that exists but cannot be read falls through to the built-in rather
// than failing, which is what the Go implementation's `if err == nil` does.
[[nodiscard]] std::expected<std::string, error::Error> read(const std::filesystem::path& root,
                                                            std::string_view family,
                                                            std::string_view kind,
                                                            std::string_view name) {
    if (!root.empty()) {
        if (auto local = detail::read_file(detail::override_path(root, kind, name))) {
            return *std::move(local);
        }
    }
    if (const auto builtin = builtin_read(family, kind, name)) {
        return std::string(*builtin);
    }
    return std::unexpected(error::Error("no such template: " + std::string(family) + "/" +
                                        std::string(kind) + "/" + std::string(name)));
}

namespace detail {

// has_component_source reports whether directory <kind> carries the source file(s)
// that mark it a usable library-component kind for family. In the headers family a
// kind is either header-only (source.hpp.tmpl) or compiled — a declaration /
// definition pair. Module kinds carry a single interface unit (source.cppm.tmpl).
[[nodiscard]] bool has_component_source(const std::filesystem::path& root,
                                        std::string_view family, std::string_view kind) {
    if (family == "headers") {
        return exists(root, family, kind, "source.hpp.tmpl") || is_compiled(root, family, kind);
    }
    return exists(root, family, kind, "source.cppm.tmpl");
}

}  // namespace detail

// kinds lists the library-component template kinds available to a project for the
// given family: the union of built-in kinds and project-local ones, minus the
// special app / test / project directories. A kind qualifies only if it carries the
// family's component source file.
[[nodiscard]] std::vector<std::string> kinds(const std::filesystem::path& root,
                                             std::string_view family) {
    // std::set both dedupes the union and sorts it, matching Go's map + sort.Strings.
    std::set<std::string> seen;
    const auto add = [&](const std::string& name) {
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
        // A missing or unreadable .cup/templates simply contributes nothing, which
        // is what Go's discarded os.ReadDir error amounts to.
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

// copy_builtin writes every file of a built-in template <family>/<kind> into dst,
// so a project can start from a copy of a default and edit it.
[[nodiscard]] std::expected<void, error::Error> copy_builtin(std::string_view family,
                                                             std::string_view kind,
                                                             const std::filesystem::path& dst) {
    std::error_code ec;
    std::filesystem::create_directories(dst, ec);
    if (ec) {
        return std::unexpected(error::Error("creating " + dst.string() + ": " + ec.message()));
    }

    const auto files = builtin_files(family, kind);
    if (files.empty()) {
        return std::unexpected(error::Error("no such template kind: " + std::string(family) + "/" +
                                            std::string(kind)));
    }
    for (const auto& file : files) {
        const std::filesystem::path out = dst / file.name;
        std::ofstream os(out, std::ios::binary | std::ios::trunc);
        os << file.content;
        if (!os) {
            return std::unexpected(error::Error("writing " + out.string()));
        }
    }
    return {};
}

}  // namespace cup::tmpl
