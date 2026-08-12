module;
#include <cstddef>
#include <expected>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
export module cup.scaffold:compiler;

export import cup.error;

export namespace cup::scaffold {

// kGuardStart and kGuardEnd delimit the cup-managed compiler-version check
// inside a project's root CMakeLists.txt. They let `cup compiler` find and
// rewrite the block in place without disturbing the rest of the file.
inline constexpr std::string_view kGuardStart = "# >>> cup:compiler-guard >>>";
inline constexpr std::string_view kGuardEnd = "# <<< cup:compiler-guard <<<";

// min_compilers returns cup's default minimum GCC and Clang major versions for
// a C++ standard — the floor baked into a new project's cup.toml. They track
// the oldest release cup expects to build each standard end to end (C++23's
// `import std;` needs GCC 15; named modules on C++20 need GCC 11 / Clang 16).
[[nodiscard]] constexpr std::pair<int, int> min_compilers(int std) {
    if (std >= 23) {
        return {15, 17};
    }
    if (std >= 20) {
        return {11, 16};
    }
    if (std >= 17) {
        return {7, 5};
    }
    return {5, 4};
}

namespace detail {

// range_up lists the integers from..to inclusive, oldest first. If to has
// fallen behind from (a baseline newer than the recorded newest), it yields
// just from.
[[nodiscard]] std::vector<int> range_up(int from, int to) {
    if (to < from) {
        to = from;
    }
    std::vector<int> out;
    out.reserve(static_cast<std::size_t>(to - from + 1));
    for (int v = from; v <= to; ++v) {
        out.push_back(v);
    }
    return out;
}

// guard_branch renders one `(...) message(FATAL_ERROR ...)` clause, shared by
// the leading if() and any trailing elseif(). id is the CMake compiler id,
// label the human name, flag the `cup compiler set` argument.
[[nodiscard]] std::string guard_branch(std::string_view id, std::string_view label,
                                       std::string_view flag, int version) {
    const std::string version_str = std::to_string(version);
    std::string out;
    out += "(CMAKE_CXX_COMPILER_ID STREQUAL \"";
    out += id;
    out += "\" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS ";
    out += version_str;
    out += ")\n  message(FATAL_ERROR \"cup: this project requires ";
    out += label;
    out += " >= ";
    out += version_str;
    out += " (have ${CMAKE_CXX_COMPILER_VERSION}). Lower the floor with `cup compiler set ";
    out += flag;
    out += " <version>`.\")";
    return out;
}

}

// compiler_choices returns the GCC and Clang major versions selectable as a
// minimum for std, oldest first. Each list starts at the baseline that first
// builds the standard (cup's curated default, min_compilers) and runs up to
// the newest released major (newest_gcc / newest_clang, discovered live by
// NewestCompilers), so `cup new` offers only compilers that can build the
// chosen standard, without a hardcoded ceiling that rots as toolchains ship.
[[nodiscard]] std::pair<std::vector<int>, std::vector<int>> compiler_choices(int std, int newest_gcc,
                                                                             int newest_clang) {
    const auto [base_gcc, base_clang] = min_compilers(std);
    return {detail::range_up(base_gcc, newest_gcc), detail::range_up(base_clang, newest_clang)};
}

// compiler_guard renders the marker-delimited CMake block that halts a build
// when the active compiler is older than the project's floor. gcc and clang
// are minimum major versions; a zero disables the check for that compiler.
// The block always carries both markers so `cup compiler` can rewrite it even
// when it currently enforces nothing.
[[nodiscard]] std::string compiler_guard(int gcc, int clang) {
    std::string out;
    out += kGuardStart;
    out += "\n";
    out += "# Minimum compiler versions, managed by `cup compiler`. Building with an older\n";
    out += "# toolchain stops here instead of failing deep in a compile. Change a floor with\n";
    out += "# `cup compiler set gcc|clang <version>` (docker-verified before it is committed).\n";

    std::vector<std::string> branches;
    if (gcc > 0) {
        branches.push_back(detail::guard_branch("GNU", "GCC", "gcc", gcc));
    }
    if (clang > 0) {
        branches.push_back(detail::guard_branch("Clang", "Clang", "clang", clang));
    }
    if (!branches.empty()) {
        out += "if";
        for (std::size_t i = 0; i < branches.size(); ++i) {
            if (i != 0) {
                out += "\nelseif";
            }
            out += branches[i];
        }
        out += "\nendif()\n";
    }
    out += kGuardEnd;
    return out;
}

// replace_compiler_guard rewrites the compiler-guard block in the CMakeLists
// at path to enforce the given minimums, leaving the rest of the file
// untouched. It errors if the file has no guard markers (e.g. a hand-written
// CMakeLists). Declared here, defined in Compiler.cpp: it needs file I/O and
// imports cup.ui for the status line, and this interface partition otherwise
// stays off the heavy-header list (see cmake.cppm's note on why that budget is
// already spent).
[[nodiscard]] std::expected<void, error::Error> replace_compiler_guard(const std::string& root,
                                                                       const std::string& path,
                                                                       int gcc, int clang);

}
