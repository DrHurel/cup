// Implementation unit for cup.scaffold:compiler — the compiler floor a project
// enforces, and the CMake block that enforces it. Port of
// internal/scaffold/compiler.go.
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
#include <string>
#include <string_view>
#include <vector>
module cup.scaffold;

import cup.ui;

namespace cup::scaffold {
namespace {

// range_up lists the integers from..to inclusive, oldest first. If to has fallen
// behind from — a baseline newer than the recorded newest release — it yields just
// from, so the picker is never empty.
[[nodiscard]] std::vector<int> range_up(int from, int to) {
    std::vector<int> out;
    for (int version = from; version <= std::max(from, to); ++version) {
        out.push_back(version);
    }
    return out;
}

// guard_branch renders one `(...) message(FATAL_ERROR ...)` clause, shared by the
// leading if() and any trailing elseif(). id is the CMake compiler id, label the
// human name, flag the `cup compiler set` argument.
//
// The doubled braces in the format string are how {} escapes itself: what lands in
// the file is ${CMAKE_CXX_COMPILER_VERSION}, CMake's own variable reference. The
// text has to match the Go implementation byte for byte — it is in every golden
// tree, and Phase 5 diffs those.
[[nodiscard]] std::string guard_branch(std::string_view id, std::string_view label,
                                       std::string_view flag, int version) {
    return std::format(
        "(CMAKE_CXX_COMPILER_ID STREQUAL \"{}\" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS {})\n"
        "  message(FATAL_ERROR \"cup: this project requires {} >= {} "
        "(have ${{CMAKE_CXX_COMPILER_VERSION}}). "
        "Lower the floor with `cup compiler set {} <version>`.\")",
        id, version, label, version, flag);
}

}  // namespace

Compilers min_compilers(int standard) {
    if (standard >= 23) {
        return {.gcc = 15, .clang = 17};
    }
    if (standard >= 20) {
        return {.gcc = 11, .clang = 16};
    }
    if (standard >= 17) {
        return {.gcc = 7, .clang = 5};
    }
    // C++14 and C++11 share a floor: the oldest compilers cup still expects to work.
    return {.gcc = 5, .clang = 4};
}

CompilerChoices compiler_choices(int standard, int newest_gcc, int newest_clang) {
    const Compilers baseline = min_compilers(standard);
    return {.gcc = range_up(baseline.gcc, newest_gcc),
            .clang = range_up(baseline.clang, newest_clang)};
}

std::string compiler_guard(int gcc, int clang) {
    std::string guard(kGuardStart);
    guard +=
        "\n"
        "# Minimum compiler versions, managed by `cup compiler`. Building with an older\n"
        "# toolchain stops here instead of failing deep in a compile. Change a floor with\n"
        "# `cup compiler set gcc|clang <version>` (docker-verified before it is committed).\n";

    std::vector<std::string> branches;
    if (gcc > 0) {
        branches.push_back(guard_branch("GNU", "GCC", "gcc", gcc));
    }
    if (clang > 0) {
        branches.push_back(guard_branch("Clang", "Clang", "clang", clang));
    }
    if (!branches.empty()) {
        guard += "if";
        for (std::size_t i = 0; i < branches.size(); ++i) {
            if (i > 0) {
                guard += "\nelseif";
            }
            guard += branches[i];
        }
        guard += "\nendif()\n";
    }
    guard += kGuardEnd;
    return guard;
}

std::expected<void, error::Error> replace_compiler_guard(const std::filesystem::path& root,
                                                         const std::filesystem::path& path,
                                                         int gcc, int clang) {
    const std::string relative = detail::rel(root, path);

    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::unexpected(error::Error(std::format("cannot update {}: not found", relative)));
    }
    const std::string content((std::istreambuf_iterator<char>(in)),
                              std::istreambuf_iterator<char>());

    const std::size_t start = content.find(kGuardStart);
    const std::size_t end = content.find(kGuardEnd);
    if (start == std::string::npos || end == std::string::npos || end < start) {
        return std::unexpected(error::Error(
            std::format("no cup compiler-guard block in {} (markers \"{}\"..\"{}\" missing)",
                        relative, kGuardStart, kGuardEnd)));
    }

    std::string updated = content.substr(0, start);
    updated += compiler_guard(gcc, clang);
    updated += content.substr(end + kGuardEnd.size());

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << updated;
    if (!out) {
        return std::unexpected(error::Error(std::format("writing {}", relative)));
    }
    ui::updated(std::format("{}  (compiler floor)", relative));
    return {};
}

}  // namespace cup::scaffold
