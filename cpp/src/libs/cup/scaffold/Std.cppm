module;
// Declarations only; the definitions are in Std.cpp. See the note at the top of
// scaffold.cppm for the three GCC 14 constraints that shape every partition here.
#include <array>
#include <expected>
#include <functional>
#include <map>
#include <string>
#include <string_view>
export module cup.scaffold:std;

// Re-exported: utils::error::Error is the E of parse_std's result.
export import utils.error;

export namespace cup::scaffold {

// Vars is the {{placeholder}} -> value map a template is rendered with.
// (Go: map[string]string.)
//
// std::less<> so a std::string_view can be looked up without building a string,
// and std::map rather than a hash map because render() iterates it: an ordered
// walk makes a rendered file depend only on its inputs, which the Phase 5
// cross-validation harness needs.
using Vars = std::map<std::string, std::string, std::less<>>;

// kStandards lists the C++ standards cup can scaffold, newest first — the order
// offered by `cup new`, whose first entry is the default. (Go: Standards.)
inline constexpr std::array<int, 5> kStandards{23, 20, 17, 14, 11};

// std_label renders a standard as it appears in the picker, e.g. 23 -> "c++23".
[[nodiscard]] std::string std_label(int standard);

// parse_std reads a standard from a picker label ("c++23") or a bare number
// ("23"), rejecting anything cup does not scaffold.
[[nodiscard]] std::expected<int, utils::error::Error> parse_std(std::string_view text);

// uses_modules reports whether standard supports C++ modules (C++20+). Below that,
// cup scaffolds classic headers instead.
[[nodiscard]] bool uses_modules(int standard);

// family maps a standard onto its template family: "modules" for C++20/23,
// "headers" for C++11/14/17. The family selects which embedded template subtree
// cup renders from.
[[nodiscard]] std::string_view family(int standard);

// std_vars returns the per-standard template variables shared by the app, test and
// library-component templates so no template hard-codes a standard:
//
//   - std_number  the bare standard, for `cxx_std_<n>` (e.g. "23")
//   - std_lib     standard-library access from a plain translation unit
//                 (app/test): `import std;` with the std module, a #include
//                 without it
//   - hello       the statement printing the app's greeting
//
// Module-interface (.cppm) templates additionally use std_prelude (a global module
// fragment carrying standard-library #includes, placed before the module
// declaration) and std_import (`import std;`, placed after it). Exactly one of the
// two is set, chosen by std_module — the project's Config::uses_std_module(). It is
// a parameter rather than a function of the standard because C++23 can be built
// either way: `import std;` needs GCC 15 and CMake 3.30, so a C++23 project on a
// GCC 14 floor keeps the global module fragment while still getting C++23's
// std::println.
//
// That middle case is the one cup itself is built with. If it regresses, `cup add`
// starts writing sources cup's own compiler floor cannot compile.
[[nodiscard]] Vars std_vars(int standard, bool std_module);

}  // namespace cup::scaffold
