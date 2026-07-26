module;
#include <cctype>
#include <expected>
#include <functional>
#include <string>
#include <string_view>
export module cup.ui:prompt;

import :io;
import :color;
// Re-exported: cup::error::Error is the E of every prompt's return type.
export import cup.error;

export namespace cup::ui {

// Validator reports whether an entered value is acceptable. On rejection its
// message is shown and the prompt repeats. (Go: func(string) error.)
using Validator = std::function<std::expected<void, error::Error>(std::string_view)>;

namespace detail {

// trim strips leading and trailing ASCII whitespace, matching strings.TrimSpace
// closely enough for prompt input.
[[nodiscard]] std::string trim(std::string_view s) {
    constexpr std::string_view kSpace = " \t\n\r\f\v";
    const auto first = s.find_first_not_of(kSpace);
    if (first == std::string_view::npos) {
        return {};
    }
    const auto last = s.find_last_not_of(kSpace);
    return std::string(s.substr(first, last - first + 1));
}

// lower returns an ASCII-lowercased copy, for matching y/n answers.
[[nodiscard]] std::string lower(std::string_view s) {
    std::string out(s);
    for (char& c : out) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

}  // namespace detail

// text prompts for a line of input. An empty entry falls back to def. validate, if
// set, must accept the value; otherwise its message is shown and the prompt
// repeats. Aborts (Ctrl+D / EOF) surface as the abort sentinel.
[[nodiscard]] std::expected<std::string, error::Error> text(
    std::string_view question, std::string_view def, const Validator& validate = {}) {
    while (true) {
        std::string line = color(bold(kCyan), "?") + " " + std::string(question) + " ";
        if (!def.empty()) {
            line += color(kGrey, "[" + std::string(def) + "]") + " ";
        }
        emit(line);
        flush_output();

        std::string entered;
        if (!read_line(entered)) {
            emit_line("");
            return std::unexpected(error::abort_error());
        }

        std::string value = detail::trim(entered);
        if (value.empty()) {
            value = std::string(def);
        }
        if (validate) {
            if (const auto ok = validate(value); !ok) {
                err("  " + ok.error().message());
                continue;
            }
        }
        return value;
    }
}

// confirm asks a yes/no question, returning def on an empty answer and repeating
// on anything it does not recognise.
[[nodiscard]] std::expected<bool, error::Error> confirm(std::string_view question, bool def) {
    const std::string_view hint = def ? "Y/n" : "y/N";
    while (true) {
        emit(color(bold(kCyan), "?") + " " + std::string(question) + " [" + std::string(hint) +
             "] ");
        flush_output();

        std::string entered;
        if (!read_line(entered)) {
            emit_line("");
            return std::unexpected(error::abort_error());
        }

        const std::string answer = detail::lower(detail::trim(entered));
        if (answer.empty()) {
            return def;
        }
        if (answer == "y" || answer == "yes") {
            return true;
        }
        if (answer == "n" || answer == "no") {
            return false;
        }
    }
}

}  // namespace cup::ui
