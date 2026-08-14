module;
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <ios>
#include <iterator>
#include <string>
module cup.scaffold;

import cup.ui;

namespace cup::scaffold {

std::expected<void, error::Error> replace_compiler_guard(const std::filesystem::path& root,
                                                          const std::filesystem::path& path,
                                                          int gcc, int clang) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::unexpected(
            error::Error(std::format("cannot update {}: no such file", rel(root, path))));
    }
    const std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const auto start = content.find(kGuardStart);
    const auto end = content.find(kGuardEnd);
    if (start == std::string::npos || end == std::string::npos || end < start) {
        return std::unexpected(error::Error(std::format(
            R"(no cup compiler-guard block in {} (markers "{}".."{}" missing))", rel(root, path),
            kGuardStart, kGuardEnd)));
    }
    const auto guard_end = end + kGuardEnd.size();
    const std::string updated =
        content.substr(0, start) + compiler_guard(gcc, clang) + content.substr(guard_end);

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << updated;
    if (!out) {
        return std::unexpected(error::Error(std::format("writing {}", path.string())));
    }
    ui::updated(std::format("{}  (compiler floor)", rel(root, path)));
    return {};
}

}
