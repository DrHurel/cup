module;
#include <cctype>
#include <expected>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
export module ui:prompt;

import :io;
import :color;
export import error;
import core;

export namespace ui {

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

// Shared by text(), select_one() and confirm(): true only when stdin is a
// real terminal *and* raw mode can actually be entered. The RawMode this
// probes with restores the original termios immediately (it is a temporary,
// destroyed at the end of the full-expression) — FTXUI enters and exits raw
// mode itself when the interactive seam actually runs, so this is a
// capability check only, not the thing that puts the terminal in raw mode.
[[nodiscard]] bool can_use_interactive(int fd) {
    if (!core::is_tty(fd)) {
        return false;
    }
    return core::enter_raw_mode(fd).has_value();
}

// text_interactive_impl is declared here, defined in Interactive.cpp: it
// drives an FTXUI ftxui::Input, which stays out of this interface partition
// for the same reason curl stays out of :net (see posix/Http.cpp).
[[nodiscard]] std::expected<std::string, error::Error> text_interactive_impl(
    std::string_view question, std::string_view def, const Validator& validate);

}

using TextInteractiveFunc = std::function<std::expected<std::string, error::Error>(
    std::string_view, std::string_view, const Validator&)>;

// text_interactive_func is the seam text() calls through on a real
// terminal; overridable in tests so callers can be tested without driving a
// real pty. Mirrors core::run_command_func().
[[nodiscard]] TextInteractiveFunc& text_interactive_func() {
    static TextInteractiveFunc f = &detail::text_interactive_impl;
    return f;
}

[[nodiscard]] std::expected<std::string, error::Error> text(
    std::string_view question, std::string_view def, const Validator& validate = {}) {
    if (detail::can_use_interactive(core::kStdinFd)) {
        return text_interactive_func()(question, def, validate);
    }

    while (true) {
        std::string line = format_text("{} {} ", color(bold(kCyan), "?"), question);
        if (!def.empty()) {
            line += format_text("{} ", color(kGrey, format_text("[{}]", def)));
        }
        emit(line);
        flush_output();

        // Filled in place rather than via expected::transform: transform on the
        // temporary moves its value into the lambda, and SonarCloud's CFamily
        // symbolic execution tied that moved-from value to `answer`, flagging
        // rejection()'s read of it as a use-after-move (cpp:S5272).
        auto answer = detail::read_answer();
        if (!answer.has_value()) {
            return answer;
        }
        if (answer->empty()) {
            *answer = def;
        }
        const auto refused = detail::rejection(validate, *answer);
        if (!refused.has_value()) {
            return answer;
        }
        err(format_text("  {}", refused->message()));
    }
}

}
