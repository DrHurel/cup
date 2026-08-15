module;
#include <array>
#include <charconv>
#include <cstddef>
#include <expected>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
export module cup.ui:select;

import :io;
import :color;
import :prompt;
export import cup.error;
import cup.platform;

export namespace cup::ui {

namespace detail {

[[nodiscard]] std::size_t index_of(std::span<const std::string> options,
                                   std::string_view want) {
    for (std::size_t i = 0; i < options.size(); ++i) {
        if (options[i] == want) {
            return i;
        }
    }
    return 0;
}

// select_interactive_impl is declared here, defined in Interactive.cpp: it
// drives an FTXUI ftxui::Menu, which stays out of this interface partition
// for the same reason curl stays out of :net (see platform/Http.cpp) — a
// heavy third-party header would otherwise enter every importer's BMI.
[[nodiscard]] std::expected<std::string, error::Error> select_interactive_impl(
    std::string_view question, std::span<const std::string> options, std::size_t initial_cursor);

[[nodiscard]] std::optional<std::size_t> parse_choice(std::string_view answer,
                                                      std::size_t count) {
    std::size_t index = 0;
    const auto [ptr, ec] = std::from_chars(answer.data(), answer.data() + answer.size(), index);
    if (ec != std::errc{} || ptr != answer.data() + answer.size() || index < 1 || index > count) {
        return std::nullopt;
    }
    return index;
}

[[nodiscard]] std::expected<std::string, error::Error> select_numbered(
    std::string_view question, std::span<const std::string> options) {
    emit_line(question);
    for (std::size_t i = 0; i < options.size(); ++i) {
        emit_numbered_line(i + 1, options[i]);
    }
    while (true) {
        auto choice = text("choice number?", "1").transform([options](const std::string& answer) {
            return parse_choice(answer, options.size());
        });
        if (!choice.has_value()) {
            return std::unexpected(std::move(choice).error());
        }
        if (choice->has_value()) {
            return options[**choice - 1];
        }
    }
}

}

using SelectInteractiveFunc = std::function<std::expected<std::string, error::Error>(
    std::string_view, std::span<const std::string>, std::size_t)>;

// select_interactive_func is the seam select_one and confirm call through on
// a real terminal; overridable in tests so callers can be tested without
// driving a real pty. Mirrors platform::run_command_func().
[[nodiscard]] SelectInteractiveFunc& select_interactive_func() {
    static SelectInteractiveFunc f = &detail::select_interactive_impl;
    return f;
}

[[nodiscard]] std::expected<std::string, error::Error> select_one(
    std::string_view question, std::span<const std::string> options, std::string_view def) {
    if (options.empty()) {
        return std::unexpected(error::Error("no options to choose from"));
    }
    if (!detail::can_use_interactive(platform::kStdinFd)) {
        return detail::select_numbered(question, options);
    }
    return select_interactive_func()(question, options, detail::index_of(options, def));
}

// Shares select_interactive_func's menu widget with entries = {"Yes", "No"}
// rather than a separate FTXUI component — one real interactive
// implementation, not two.
[[nodiscard]] std::expected<bool, error::Error> confirm(std::string_view question, bool def) {
    if (detail::can_use_interactive(platform::kStdinFd)) {
        const std::array<std::string, 2> options{"Yes", "No"};
        auto chosen = select_interactive_func()(question, options, def ? 0 : 1);
        if (!chosen.has_value()) {
            return std::unexpected(std::move(chosen).error());
        }
        return *chosen == "Yes";
    }

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
