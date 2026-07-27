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
// Re-exported: utils::error::Error is the E of every result below.
export import utils.error;

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

// local_read returns a project-local override's bytes, or nullopt when there is no
// project root, no such file, or the file cannot be read. Those three cases are
// deliberately one: each means "fall through to the built-in", which is what the Go
// implementation's `if err == nil` amounts to.
[[nodiscard]] std::optional<std::string> local_read(const std::filesystem::path& root,
                                                    std::string_view kind, std::string_view name) {
    return root.empty() ? std::nullopt : read_file(override_path(root, kind, name));
}

// write_file writes content to path verbatim, truncating what was there.
[[nodiscard]] std::expected<void, utils::error::Error> write_file(const std::filesystem::path& path,
                                                                  std::string_view content) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << content;
    if (!out) {
        return std::unexpected(utils::error::Error(std::format("writing {}", path.string())));
    }
    return {};
}

// create_dir makes dst and its parents, putting the failure in the error channel so
// it can head a chain.
[[nodiscard]] std::expected<void, utils::error::Error> create_dir(
    const std::filesystem::path& dst) {
    std::error_code ec;
    std::filesystem::create_directories(dst, ec);
    if (ec) {
        return std::unexpected(
            utils::error::Error(std::format("creating {}: {}", dst.string(), ec.message())));
    }
    return {};
}

}  // namespace detail

// exists reports whether template file <kind>/<name> is available for family,
// either as a project-local override or a built-in default. root may be empty to
// consult only the built-ins.
[[nodiscard]] bool exists(const std::filesystem::path& root, std::string_view family,
                          std::string_view kind, std::string_view name) {
    // std::error_code overload: a permission failure mid-walk reports "not there"
    // rather than throwing, matching os.Stat's error return in Go.
    std::error_code ec;
    return (!root.empty() &&
            std::filesystem::exists(detail::override_path(root, kind, name), ec)) ||
           builtin_exists(family, kind, name);
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
// The preference is spelled or_else because that is exactly what it is: the
// built-in is consulted only when the override yields nothing, and require then
// turns "neither" into the one error this can report.
[[nodiscard]] std::expected<std::string, utils::error::Error> read(
    const std::filesystem::path& root, std::string_view family, std::string_view kind,
    std::string_view name) {
    auto resolved = detail::local_read(root, kind, name).or_else([family, kind, name] {
        return builtin_read(family, kind, name).transform(
            [](std::string_view content) { return std::string(content); });
    });
    return utils::error::require(std::move(resolved),
                                 std::format("no such template: {}/{}/{}", family, kind, name));
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
    // std::less<> so a string_view can be looked up without building a string.
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

namespace detail {

// kind_files lists the built-in files of <family>/<kind>. The corpus reports an
// unknown kind by returning nothing at all, so the emptiness is turned back into
// the error it stands for before the chain goes any further.
[[nodiscard]] std::expected<std::vector<BuiltinFile>, utils::error::Error> kind_files(
    std::string_view family, std::string_view kind) {
    auto files = builtin_files(family, kind);
    return utils::error::require(files.empty() ? std::nullopt : std::optional(std::move(files)),
                                 std::format("no such template kind: {}/{}", family, kind));
}

}  // namespace detail

// copy_builtin writes every file of a built-in template <family>/<kind> into dst,
// so a project can start from a copy of a default and edit it.
//
// The three steps stay in this order because the errors depend on it: dst is
// created before the corpus is consulted, so an unwritable destination is reported
// as such even when the kind does not exist either.
[[nodiscard]] std::expected<void, utils::error::Error> copy_builtin(
    std::string_view family, std::string_view kind, const std::filesystem::path& dst) {
    return detail::create_dir(dst)
        .and_then([family, kind] { return detail::kind_files(family, kind); })
        .and_then([&dst](const std::vector<BuiltinFile>& files) {
            return utils::error::for_each(files, [&dst](const BuiltinFile& file) {
                return detail::write_file(dst / file.name, file.content);
            });
        });
}

}  // namespace cup::tmpl
