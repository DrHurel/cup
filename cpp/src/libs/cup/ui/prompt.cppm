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
export import cup.error;

export namespace cup::ui {

using Validator = std::function<std::expected<void, error::Error>(std::string_view)>;

namespace detail {

[[nodiscard]] std::string trim(std::string_view s) {
    constexpr std::string_view kSpace = " \t\n\r\f\v";
    const auto first = s.find_first_not_of(kSpace);
    if (first == std::string_view::npos) {
        return {};
    }
    const auto last = s.find_last_not_of(kSpace);
    return std::string(s.substr(first, last - first + 1));
}

[[nodiscard]] std::string lower(std::string_view s) {
    std::string out(s);
    for (char& c : out) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

[[nodiscard]] std::expected<std::string, error::Error> read_answer() {
    std::string entered;
    if (!read_line(entered)) {
        emit_line("");
        return std::unexpected(error::abort_error());
    }
    return trim(entered);
}

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

}

[[nodiscard]] std::expected<std::string, error::Error> text(
    std::string_view question, std::string_view def, const Validator& validate = {}) {
    while (true) {
        std::string line = format_text("{} {} ", color(bold(kCyan), "?"), question);
        if (!def.empty()) {
            line += format_text("{} ", color(kGrey, format_text("[{}]", def)));
        }
        emit(line);
        flush_output();

        // Named distinctly from rejection()'s `value` parameter below: SonarCloud's
        // CFamily symbolic execution conflated the two identically-named locals
        // across the call graph and flagged rejection()'s (never moved) `value` as
        // used-after-move because of this lambda's own std::move(entered).
        auto answer = detail::read_answer().transform([def](std::string entered) {
            return entered.empty() ? std::string(def) : std::move(entered);
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
        if (const auto decided = detail::decide(*answer, def); decided.has_value()) {
            return *decided;
        }
    }
}

}
