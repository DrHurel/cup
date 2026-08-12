module;
#include <array>
#include <expected>
#include <map>
#include <optional>
#include <string>
#include <string_view>
export module cup.scaffold:std;

export import cup.error;

export namespace cup::scaffold {

// kStandards lists the C++ standards cup can scaffold, newest first — the order
// offered by `cup new` (the first is the default).
inline constexpr std::array<int, 5> kStandards{23, 20, 17, 14, 11};

// std_label renders a standard as it appears in the picker, e.g. 23 -> "c++23".
[[nodiscard]] std::string std_label(int std) { return "c++" + std::to_string(std); }

namespace detail {

// Parses a plain (non-"c++"-prefixed) decimal integer, rejecting anything that
// is not entirely digits (with an optional leading sign) — no <charconv>, to
// keep this partition off the heavy-header list (see naming.cppm's rel_parts).
[[nodiscard]] std::optional<int> parse_int(std::string_view s) {
    if (s.empty()) {
        return std::nullopt;
    }
    std::size_t i = 0;
    bool negative = false;
    if (s.front() == '-' || s.front() == '+') {
        negative = s.front() == '-';
        i = 1;
    }
    if (i == s.size()) {
        return std::nullopt;
    }
    int n = 0;
    for (; i < s.size(); ++i) {
        if (s[i] < '0' || s[i] > '9') {
            return std::nullopt;
        }
        n = n * 10 + (s[i] - '0');
    }
    return negative ? -n : n;
}

}

// parse_std reads a standard from a picker label ("c++23") or a bare number
// ("23"), rejecting anything cup does not scaffold.
[[nodiscard]] std::expected<int, error::Error> parse_std(std::string_view s) {
    if (s.size() > 3 && s.substr(0, 3) == "c++") {
        s = s.substr(3);
    }
    if (const auto n = detail::parse_int(s); n.has_value()) {
        for (const int std : kStandards) {
            if (std == *n) {
                return *n;
            }
        }
    }
    return std::unexpected(error::Error("unknown C++ standard \"" + std::string(s) + "\""));
}

// uses_modules reports whether std supports C++ modules (C++20+). Below that,
// cup scaffolds classic headers instead.
[[nodiscard]] constexpr bool uses_modules(int std) { return std >= 20; }

// family maps a standard onto its template family: "modules" for C++20/23,
// "headers" for C++11/14/17. The family selects which embedded template
// subtree (files/<family>/…) cup renders from.
[[nodiscard]] constexpr std::string_view family(int std) {
    return uses_modules(std) ? "modules" : "headers";
}

// std_vars returns the per-standard template variables shared by the app, test,
// and library-component templates so no template hard-codes a standard:
//
//   - std_number  the bare standard, for `cxx_std_<n>` (e.g. "23")
//   - std_lib     standard-library access from a plain translation unit
//     (app/test): `import std;` with the std module, a #include without it
//   - hello       the statement printing the app's greeting
//
// Module-interface (.cppm) templates additionally use std_prelude (a global
// module fragment carrying standard-library #includes, placed before the
// module declaration) and std_import (`import std;`, placed after the
// declaration). Exactly one of the two is set, chosen by std_module — the
// project's Config::uses_std_module(). It is a parameter rather than a
// function of std because C++23 can be built either way: `import std;` needs
// GCC 15 and CMake 3.30, so a C++23 project on a GCC 14 floor keeps the global
// module fragment while still getting C++23's std::println.
[[nodiscard]] std::map<std::string, std::string> std_vars(int std, bool std_module) {
    std::map<std::string, std::string> vars{
        {"std_number", std::to_string(std)},
    };
    if (std >= 23 && std_module) {
        vars["std_lib"] = "import std;";
        vars["std_prelude"] = "";
        // Surrounding blank lines so the .cppm greeting matches C++23's original
        // spacing; empty std_prelude leaves nothing before the module decl.
        vars["std_import"] = "\nimport std;\n";
        vars["hello"] = "std::println(\"Hello from {{name}}!\");";
    } else if (std >= 23) {
        // C++23 without the std module: std::println comes from <print>, which is
        // the standard header — only the *module* form of the library is missing.
        vars["std_lib"] = "#include <print>";
        vars["std_prelude"] = "module;\n#include <print>\n";
        vars["std_import"] = "";
        vars["hello"] = "std::println(\"Hello from {{name}}!\");";
    } else if (std >= 20) {
        vars["std_lib"] = "#include <iostream>";
        vars["std_prelude"] = "module;\n#include <iostream>\n";
        vars["std_import"] = "";
        vars["hello"] = "std::cout << \"Hello from {{name}}!\\n\";";
    } else {
        vars["std_lib"] = "#include <iostream>";
        vars["hello"] = "std::cout << \"Hello from {{name}}!\\n\";";
    }
    return vars;
}

}
