module;
// Declarations only; the definitions are in Compiler.cpp. See the note at the top
// of scaffold.cppm.
#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>
export module cup.scaffold:compiler;

// Re-exported: utils::error::Error is the E of replace_compiler_guard's result.
export import utils.error;

export namespace cup::scaffold {

// kGuardStart and kGuardEnd delimit the cup-managed compiler-version check inside a
// project's root CMakeLists.txt. They let `cup compiler` find and rewrite the block
// in place without disturbing the rest of the file — which is why they are part of
// the interface: the markers are a contract with every CMakeLists cup has ever
// written.
inline constexpr std::string_view kGuardStart = "# >>> cup:compiler-guard >>>";
inline constexpr std::string_view kGuardEnd = "# <<< cup:compiler-guard <<<";

// Compilers is a GCC/Clang pair of major versions, where 0 means "no floor". Go
// returns the two as unnamed results; naming them here is what keeps
// `min_compilers(std).clang` from being a positional guess at the call site.
struct Compilers {
    int gcc = 0;
    int clang = 0;

    // Spelled out rather than `= default` for the reason DockerImage::operator==
    // gives in cup.project: GCC 16 segfaults serialising a defaulted friend
    // operator== into a module interface.
    friend bool operator==(const Compilers& lhs, const Compilers& rhs) {
        return lhs.gcc == rhs.gcc && lhs.clang == rhs.clang;
    }
};

// CompilerChoices is what the `cup new` floor picker offers, oldest first.
struct CompilerChoices {
    std::vector<int> gcc;
    std::vector<int> clang;
};

// min_compilers returns cup's default minimum GCC and Clang major versions for a
// C++ standard — the floor baked into a new project's cup.toml. They track the
// oldest release cup expects to build each standard end to end (C++23's
// `import std;` needs GCC 15; named modules on C++20 need GCC 11 / Clang 16).
//
// It is a *default*, and a project's own [compiler] table wins over it: cup's own
// cpp/cup.toml pins GCC 14 for C++23 precisely because it declines the std module.
[[nodiscard]] Compilers min_compilers(int standard);

// compiler_choices returns the GCC and Clang major versions selectable as a minimum
// for standard, oldest first. Each list starts at the baseline that first builds the
// standard (min_compilers) and runs up to the newest released major (from
// newest_compilers), so `cup new` offers only compilers that can build the chosen
// standard, without a hardcoded ceiling that rots as toolchains ship.
[[nodiscard]] CompilerChoices compiler_choices(int standard, int newest_gcc, int newest_clang);

// compiler_guard renders the marker-delimited CMake block that halts a build when
// the active compiler is older than the project's floor. A zero version disables the
// check for that compiler. The block always carries both markers, so `cup compiler`
// can rewrite it even when it currently enforces nothing.
[[nodiscard]] std::string compiler_guard(int gcc, int clang);

// replace_compiler_guard rewrites the compiler-guard block in the CMakeLists at
// path to enforce the given minimums, leaving the rest of the file untouched. It
// fails if the file has no guard markers — a hand-written CMakeLists, or one cup
// has never managed.
[[nodiscard]] std::expected<void, utils::error::Error> replace_compiler_guard(
    const std::filesystem::path& root, const std::filesystem::path& path, int gcc, int clang);

}  // namespace cup::scaffold
