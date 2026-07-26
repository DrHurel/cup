module;
#include <cctype>
#include <expected>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
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

// read_answer reads one trimmed line. End of input — Ctrl+D, or a script that ran
// out — is the *only* failure a prompt has, so it is the only thing this puts in
// the error channel; everything the user can type is a value, and what to do with
// an unacceptable one is each prompt's own business.
[[nodiscard]] std::expected<std::string, error::Error> read_answer() {
    std::string entered;
    if (!read_line(entered)) {
        emit_line("");
        return std::unexpected(error::abort_error());
    }
    return trim(entered);
}

// rejection runs the validator, if there is one, and returns why the value was
// refused — nullopt when it was accepted. A refusal is not an error of the prompt:
// it is the signal to ask the question again, so it stays out of the expected.
[[nodiscard]] std::optional<error::Error> rejection(const Validator& validate,
                                                    const std::string& value) {
    if (!validate) {
        return std::nullopt;
    }
    auto checked = validate(value);
    if (checked.has_value()) {
        return std::nullopt;
    }
    return std::move(checked).error();
}

// decide maps a lowercased answer onto a yes/no, taking def for an empty one.
// Anything unrecognised is nullopt, and confirm asks again.
[[nodiscard]] std::optional<bool> decide(std::string_view answer, bool def) {
    if (answer.empty()) {
        return def;
    }
    if (answer == "y" || answer == "yes") {
        return true;
    }
    if (answer == "n" || answer == "no") {
        return false;
    }
    return std::nullopt;
}

}  // namespace detail

// text prompts for a line of input. An empty entry falls back to def. validate, if
// set, must accept the value; otherwise its message is shown and the prompt
// repeats. Aborts (Ctrl+D / EOF) surface as the abort sentinel.
[[nodiscard]] std::expected<std::string, error::Error> text(
    std::string_view question, std::string_view def, const Validator& validate = {}) {
    while (true) {
        std::string line = format_text("{} {} ", color(bold(kCyan), "?"), question);
        if (!def.empty()) {
            line += format_text("{} ", color(kGrey, format_text("[{}]", def)));
        }
        emit(line);
        flush_output();

        // Reading and defaulting are one chain; only an abort leaves it, and it
        // leaves as the caller's result. (Left non-const so both returns move.)
        auto answer = detail::read_answer().transform([def](std::string value) {
            return value.empty() ? std::string(def) : std::move(value);
        });
        if (!answer.has_value()) {
            return answer;
        }
        const auto refused = detail::rejection(validate, *answer);
        if (!refused.has_value()) {
            return answer;
        }
        err(format_text("  {}", refused->message()));
    }
}

// confirm asks a yes/no question, returning def on an empty answer and repeating
// on anything it does not recognise.
[[nodiscard]] std::expected<bool, error::Error> confirm(std::string_view question, bool def) {
    const std::string_view hint = def ? "Y/n" : "y/N";
    while (true) {
        emit(format_text("{} {} [{}] ", color(bold(kCyan), "?"), question, hint));
        flush_output();

        auto answer = detail::read_answer().transform(
            [](const std::string& entered) { return detail::lower(entered); });
        if (!answer.has_value()) {
            return std::unexpected(std::move(answer).error());
        }
        // has_value(), not the optional's own contextual conversion: the
        // contained type is bool, so `if (decided)` would read as a test of the
        // answer rather than of whether there was one.
        if (const auto decided = detail::decide(*answer, def); decided.has_value()) {
            return *decided;
        }
    }
}

}  // namespace cup::ui
