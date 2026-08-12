module;
#include <array>
#include <charconv>
#include <cstddef>
#include <expected>
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

[[nodiscard]] bool is_up_key(std::span<const unsigned char> buf, int n) {
    if (buf.empty()) {
        return false;
    }
    return buf[0] == 'k' || (n == 3 && buf[0] == 27 && buf[2] == 'A');
}

[[nodiscard]] bool is_down_key(std::span<const unsigned char> buf, int n) {
    if (buf.empty()) {
        return false;
    }
    return buf[0] == 'j' || (n == 3 && buf[0] == 27 && buf[2] == 'B');
}

void move_up(std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) {
        emit("\x{1b}[1A\x{1b}[2K");
    }
}

void render(std::span<const std::string> options, std::size_t cursor) {
    for (std::size_t i = 0; i < options.size(); ++i) {
        if (i == cursor) {
            emit(format_text("{}{}\r\n", color(bold(kCyan), "> "), color(bold(kCyan), options[i])));
        } else {
            emit(format_text("  {}\r\n", options[i]));
        }
    }
}

void redraw_final(std::span<const std::string> options, std::size_t cursor) {
    for (std::size_t i = 0; i < options.size(); ++i) {
        if (i == cursor) {
            emit(format_text("{}{}\r\n", color(bold(kGreen), "> "),
                             color(bold(kGreen), options[i])));
        } else {
            emit(format_text("  {}\r\n", color(kGrey, options[i])));
        }
    }
}

[[nodiscard]] std::expected<std::string, error::Error> select_interactive(
    std::string_view question, std::span<const std::string> options, std::size_t cursor) {
    emit(format_text("{} {} {}\r\n", color(bold(kCyan), "?"), question,
                     color(kGrey, "(up/down, enter)")));
    render(options, cursor);

    std::array<unsigned char, 3> buf{};
    while (true) {
        const int n = read_key(buf);
        if (n <= 0) {
            return std::unexpected(error::abort_error());
        }
        const std::span<const unsigned char> keys(buf.data(), static_cast<std::size_t>(n));

        if (buf[0] == 3 || buf[0] == 4) {
            return std::unexpected(error::abort_error());
        }
        if (buf[0] == '\r' || buf[0] == '\n') {
            move_up(options.size());
            redraw_final(options, cursor);
            return options[cursor];
        }
        if (is_up_key(keys, n)) {
            cursor = (cursor + options.size() - 1) % options.size();
        } else if (is_down_key(keys, n)) {
            cursor = (cursor + 1) % options.size();
        } else {
            continue;
        }
        move_up(options.size());
        render(options, cursor);
    }
}

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

[[nodiscard]] std::expected<std::string, error::Error> select_one(
    std::string_view question, std::span<const std::string> options, std::string_view def) {
    if (options.empty()) {
        return std::unexpected(error::Error("no options to choose from"));
    }
    if (!platform::is_tty(platform::kStdinFd)) {
        return detail::select_numbered(question, options);
    }
    if (auto raw = platform::enter_raw_mode(platform::kStdinFd); raw.has_value()) {
        return detail::select_interactive(question, options, detail::index_of(options, def));
    }
    return detail::select_numbered(question, options);
}

}
