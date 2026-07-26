module;
#include <array>
#include <charconv>
#include <cstddef>
#include <expected>
#include <span>
#include <string>
#include <string_view>
export module cup.ui:select;

import :io;
import :color;
import :prompt;
export import cup.error;
import cup.platform;

export namespace cup::ui {

namespace detail {

// index_of returns the position of want in options, or 0 when it is absent — so a
// default that no longer matches an option simply highlights the first one.
[[nodiscard]] std::size_t index_of(std::span<const std::string> options,
                                   std::string_view want) {
    for (std::size_t i = 0; i < options.size(); ++i) {
        if (options[i] == want) {
            return i;
        }
    }
    return 0;
}

// is_up_key / is_down_key decode a "move" press: the vim keys, or the three-byte
// ESC [ A / ESC [ B arrow sequences. n is how many bytes were actually read, which
// is what distinguishes a real arrow sequence from a lone ESC.
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

// move_up walks the cursor back over n printed lines, clearing each, so the menu
// can be redrawn in place.
void move_up(std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) {
        emit("\x1b[1A\x1b[2K");
    }
}

// render draws the option list with the cursor line highlighted.
void render(std::span<const std::string> options, std::size_t cursor) {
    for (std::size_t i = 0; i < options.size(); ++i) {
        if (i == cursor) {
            emit(color(bold(kCyan), "> ") + color(bold(kCyan), options[i]) + "\r\n");
        } else {
            emit("  " + options[i] + "\r\n");
        }
    }
}

// redraw_final reprints the list once the choice is made, marking the pick, so the
// terminal keeps a stable record after raw mode is restored.
void redraw_final(std::span<const std::string> options, std::size_t cursor) {
    for (std::size_t i = 0; i < options.size(); ++i) {
        if (i == cursor) {
            emit(color(bold(kGreen), "> ") + color(bold(kGreen), options[i]) + "\r\n");
        } else {
            emit("  " + color(kGrey, options[i]) + "\r\n");
        }
    }
}

// select_interactive drives the raw-mode arrow-key menu, starting at cursor.
[[nodiscard]] std::expected<std::string, error::Error> select_interactive(
    std::string_view question, std::span<const std::string> options, std::size_t cursor) {
    emit(color(bold(kCyan), "?") + " " + std::string(question) + " " +
         color(kGrey, "(up/down, enter)") + "\r\n");
    render(options, cursor);

    std::array<unsigned char, 3> buf{};
    while (true) {
        const int n = read_key(buf);
        if (n <= 0) {
            return std::unexpected(error::abort_error());
        }
        const std::span<const unsigned char> keys(buf.data(), static_cast<std::size_t>(n));

        if (buf[0] == 3 || buf[0] == 4) {  // Ctrl+C / Ctrl+D
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

// select_numbered is the fallback for a non-terminal stdin: a numbered list read
// from one line, so cup still works over a pipe.
[[nodiscard]] std::expected<std::string, error::Error> select_numbered(
    std::string_view question, std::span<const std::string> options) {
    emit_line(question);
    for (std::size_t i = 0; i < options.size(); ++i) {
        emit_line("  " + std::to_string(i + 1) + ") " + options[i]);
    }
    while (true) {
        const auto choice = text("choice number?", "1");
        if (!choice) {
            return std::unexpected(choice.error());
        }
        std::size_t index = 0;
        const std::string& s = *choice;
        const auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), index);
        if (ec == std::errc{} && index >= 1 && index <= options.size()) {
            return options[index - 1];
        }
    }
}

}  // namespace detail

// select_one shows an arrow-key menu and returns the chosen option. def, when it
// matches an option, starts highlighted. Off a terminal — or when raw mode cannot
// be entered — it falls back to the numbered list.
//
// Named select_one rather than select because POSIX already claims ::select.
[[nodiscard]] std::expected<std::string, error::Error> select_one(
    std::string_view question, std::span<const std::string> options, std::string_view def) {
    if (options.empty()) {
        return std::unexpected(error::Error("no options to choose from"));
    }
    if (!platform::is_tty(platform::kStdinFd)) {
        return detail::select_numbered(question, options);
    }
    auto raw = platform::enter_raw_mode(platform::kStdinFd);
    if (!raw) {
        return detail::select_numbered(question, options);
    }
    // The guard restores the terminal when it goes out of scope, on every path.
    return detail::select_interactive(question, options, detail::index_of(options, def));
}

}  // namespace cup::ui
