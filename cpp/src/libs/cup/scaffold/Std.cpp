// Implementation unit for cup.scaffold:std — the C++ standard and what it implies
// for a template. Port of internal/scaffold/std.go.
module;
#include <charconv>
#include <expected>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <format>
module cup.scaffold;

namespace cup::scaffold {

std::string std_label(int standard) { return std::format("c++{}", standard); }

std::expected<int, error::Error> parse_std(std::string_view text) {
    // Only a label longer than the prefix loses it, so a bare "c++" stays whole and
    // fails the parse below rather than becoming an empty string. (Go: the
    // len(s) > 3 guard.)
    std::string_view digits = text;
    if (digits.size() > 3 && digits.starts_with("c++")) {
        digits.remove_prefix(3);
    }

    int value = 0;
    const char* const first = digits.data();
    const char* const last = first + digits.size();
    // ptr == last rejects a trailing remainder ("2x"), which is what makes this as
    // strict as strconv.Atoi; from_chars alone would happily stop early.
    if (const auto [ptr, ec] = std::from_chars(first, last, value);
        ec == std::errc{} && ptr == last) {
        for (const int standard : kStandards) {
            if (standard == value) {
                return value;
            }
        }
    }
    // The message quotes the *stripped* text, so "c++99" reports 99 — matching Go's
    // %q of the same trimmed value.
    return std::unexpected(
        error::Error(std::string("unknown C++ standard \"").append(digits).append("\"")));
}

bool uses_modules(int standard) { return standard >= 20; }

std::string_view family(int standard) { return uses_modules(standard) ? "modules" : "headers"; }

Vars std_vars(int standard, bool std_module) {
    Vars vars{{"std_number", std::to_string(standard)}};

    if (standard >= 23 && std_module) {
        vars["std_lib"] = "import std;";
        vars["std_prelude"] = "";
        // Surrounding blank lines so the .cppm greeting matches C++23's original
        // spacing; an empty std_prelude leaves nothing before the module decl.
        vars["std_import"] = "\nimport std;\n";
        vars["hello"] = R"(std::println("Hello from {{name}}!");)";
    } else if (standard >= 23) {
        // C++23 without the std module: std::println comes from <print>, which is
        // the standard header — only the *module* form of the library is missing.
        vars["std_lib"] = "#include <print>";
        vars["std_prelude"] = "module;\n#include <print>\n";
        vars["std_import"] = "";
        vars["hello"] = R"(std::println("Hello from {{name}}!");)";
    } else if (standard >= 20) {
        vars["std_lib"] = "#include <iostream>";
        vars["std_prelude"] = "module;\n#include <iostream>\n";
        vars["std_import"] = "";
        vars["hello"] = R"(std::cout << "Hello from {{name}}!\n";)";
    } else {
        // The headers family sets neither module-only key — not even to an empty
        // string. A .cppm template is never rendered at this standard, and a key
        // that exists but is blank would hide that.
        vars["std_lib"] = "#include <iostream>";
        vars["hello"] = R"(std::cout << "Hello from {{name}}!\n";)";
    }
    return vars;
}

}  // namespace cup::scaffold
