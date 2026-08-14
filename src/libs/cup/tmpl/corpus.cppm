module;
#include <cup/embedded_templates.hpp>

#include <cstddef>
#include <functional>
#include <initializer_list>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>
export module cup.tmpl:corpus;

export namespace cup::tmpl {

namespace detail {

[[nodiscard]] std::string concat(std::initializer_list<std::string_view> segments) {
    std::size_t size = 0;
    for (const std::string_view segment : segments) {
        size += segment.size();
    }
    std::string joined;
    joined.reserve(size);
    for (const std::string_view segment : segments) {
        joined.append(segment);
    }
    return joined;
}

[[nodiscard]] std::string corpus_path(std::string_view family, std::string_view kind,
                                      std::string_view name) {
    return concat({family, "/", kind, "/", name});
}

}

struct BuiltinFile {
    std::string name;
    std::string_view content;
};

[[nodiscard]] std::optional<std::string_view> builtin_read(std::string_view family,
                                                           std::string_view kind,
                                                           std::string_view name) {
    return templates::read(detail::corpus_path(family, kind, name));
}

[[nodiscard]] bool builtin_exists(std::string_view family, std::string_view kind,
                                  std::string_view name) {
    return templates::exists(detail::corpus_path(family, kind, name));
}

[[nodiscard]] std::vector<std::string> builtin_kinds(std::string_view family) {
    const std::string prefix = detail::concat({family, "/"});
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

[[nodiscard]] std::vector<BuiltinFile> builtin_files(std::string_view family,
                                                     std::string_view kind) {
    const std::string prefix = detail::concat({family, "/", kind, "/"});
    std::vector<BuiltinFile> files;
    for (const auto& entry : templates::all()) {
        if (!entry.path.starts_with(prefix)) {
            continue;
        }
        const std::string_view name = entry.path.substr(prefix.size());
        if (name.contains('/')) {
            continue;
        }
        files.emplace_back(std::string(name), entry.content);
    }
    return files;
}

}
