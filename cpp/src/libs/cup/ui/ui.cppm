module;
#include <unistd.h>

#include <array>
#include <cctype>
#include <charconv>
#include <cstddef>
#include <cstdlib>
#include <expected>
#include <functional>
#include <iostream>
#include <istream>
#include <print>
#include <span>
#include <string>
#include <string_view>
export module cup.ui;

// Re-exported because cup::error::Error appears in every prompt's return type.
export import cup.error;
import cup.platform;

// cup.ui is cup's interactive layer: an arrow-key menu, a y/n confirm, a validated
// text input, and the coloured "wrote / updated / skipped" log.
//
// Deliberately one translation unit, rather than the :color / :prompt / :select
// partitions this started as. GCC 14 cannot produce a readable BMI for a module
// whose partitions import anything outside themselves: the *consumer* fails with
// "error: failed to read compiled module cluster N: Bad file data". It reproduces
// in about twenty lines with no CMake involved, and fires both when a partition
// imports another module and when it imports a sibling partition. Flat module
// interfaces are unaffected — so cup keeps named modules and drops partitions
// until the compiler floor moves past GCC 14.

export namespace cup::ui {

// --- colour ---------------------------------------------------------------

// ANSI SGR parameters for cup's palette. These are 256-colour codes rather than
// the basic 8, so the output looks the same across terminal themes.
inline constexpr std::string_view kCyan = "38;5;81";
inline constexpr std::string_view kGreen = "38;5;77";
inline constexpr std::string_view kOrange = "38;5;215";
inline constexpr std::string_view kGrey = "38;5;244";
inline constexpr std::string_view kRed = "38;5;203;1";
inline constexpr std::string_view kSalmon = "38;5;209";

namespace detail {

// colour_enabled holds the one piece of mutable state in cup.ui. It is seeded on
// first use from the environment — honouring NO_COLOR and refusing to colour a
// redirected stdout — and is settable so tests can exercise both branches.
bool& colour_enabled() {
    static bool enabled = std::getenv("NO_COLOR") == nullptr && platform::is_tty(STDOUT_FILENO);
    return enabled;
}

}  // namespace detail

// use_color reports whether output is currently colourised.
[[nodiscard]] bool use_color() { return detail::colour_enabled(); }

// set_use_color forces colouring on or off, returning the previous setting so a
// caller can restore it.
bool set_use_color(bool enabled) {
    const bool previous = detail::colour_enabled();
    detail::colour_enabled() = enabled;
    return previous;
}

// color wraps s in an ANSI escape, or returns it untouched when colour is off.
[[nodiscard]] std::string color(std::string_view code, std::string_view s) {
    if (!use_color()) {
        return std::string(s);
    }
    return std::string("\x1b[").append(code).append("m").append(s).append("\x1b[0m");
}

// bold builds the ";1" variant of a palette entry, used for the emphasised lines.
[[nodiscard]] std::string bold(std::string_view code) {
    return std::string(code) + ";1";
}

namespace detail {

// status prints one "  <label> <message>" line. Labels are padded to a common
// width by their callers so the messages line up in a column.
void status(std::string_view code, std::string_view label, std::string_view msg) {
    std::println("  {} {}", color(code, label), msg);
}

}  // namespace detail

// The scaffolding log. Each line names what cup did to a path, so a `cup new` or
// `cup add` run reads as a list of effects.
void running(std::string_view msg) { detail::status(kCyan, "run     ", msg); }
void wrote(std::string_view msg) { detail::status(kGreen, "wrote   ", msg); }
void updated(std::string_view msg) { detail::status(kOrange, "updated ", msg); }
void skipped(std::string_view msg) { detail::status(kGrey, "skipped ", msg); }
void removed(std::string_view msg) { detail::status(kSalmon, "removed ", msg); }
void next(std::string_view msg) { detail::status(kCyan, "next    ", msg); }

// Whole-line messages, with no status column.
void accent(std::string_view msg) { std::println("{}", color(bold(kCyan), msg)); }
void success(std::string_view msg) { std::println("{}", color(bold(kGreen), msg)); }
void err(std::string_view msg) { std::println(stderr, "{}", color(kRed, msg)); }

// --- text input -----------------------------------------------------------

// Validator reports whether an entered value is acceptable. On rejection its
// message is shown and the prompt repeats. (Go: func(string) error.)
using Validator = std::function<std::expected<void, error::Error>(std::string_view)>;

namespace detail {

// current_input is the stream the prompts read from. It defaults to std::cin and
// is redirectable, which is what lets cup be driven over a pipe — and what lets
// tests script a whole interactive session.
std::istream*& current_input() {
    static std::istream* in = &std::cin;
    return in;
}

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

// read_line reads one line, reporting false at end of input with nothing read. A
// final line without a trailing newline still counts as data, which is why the
// emptiness of the line — not the stream state alone — decides.
bool read_line(std::string& line) {
    line.clear();
    std::getline(*current_input(), line);
    return !line.empty() || !current_input()->fail();
}

}  // namespace detail

// ScopedInput redirects the prompts to another stream and restores the previous
// one when it goes out of scope. (Go: SetInput returning a restore func.)
class ScopedInput {
public:
    explicit ScopedInput(std::istream& in) : previous_(detail::current_input()) {
        detail::current_input() = &in;
    }
    ScopedInput(const ScopedInput&) = delete;
    ScopedInput& operator=(const ScopedInput&) = delete;
    ~ScopedInput() { detail::current_input() = previous_; }

private:
    std::istream* previous_;
};

// text prompts for a line of input. An empty entry falls back to def. validate, if
// set, must accept the value; otherwise its message is shown and the prompt
// repeats. Aborts (Ctrl+D / EOF) surface as the abort sentinel.
[[nodiscard]] std::expected<std::string, error::Error> text(
    std::string_view question, std::string_view def, const Validator& validate = {}) {
    while (true) {
        if (!def.empty()) {
            std::print("{} {} {} ", color(bold(kCyan), "?"), question,
                       color(kGrey, std::string("[").append(def).append("]")));
        } else {
            std::print("{} {} ", color(bold(kCyan), "?"), question);
        }
        std::cout.flush();

        std::string line;
        if (!detail::read_line(line)) {
            std::println();
            return std::unexpected(error::abort_error());
        }

        std::string value = detail::trim(line);
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
[[nodiscard]] std::expected<bool, error::Error> confirm(std::string_view question,
                                                               bool def) {
    const std::string_view hint = def ? "Y/n" : "y/N";
    while (true) {
        std::print("{} {} [{}] ", color(bold(kCyan), "?"), question, hint);
        std::cout.flush();

        std::string line;
        if (!detail::read_line(line)) {
            std::println();
            return std::unexpected(error::abort_error());
        }

        std::string answer = detail::trim(line);
        for (char& c : answer) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
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

// --- selection menu -------------------------------------------------------

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
        std::print("\x1b[1A\x1b[2K");
    }
}

// render draws the option list with the cursor line highlighted.
void render(std::span<const std::string> options, std::size_t cursor) {
    for (std::size_t i = 0; i < options.size(); ++i) {
        if (i == cursor) {
            std::print("{}{}\r\n", color(bold(kCyan), "> "), color(bold(kCyan), options[i]));
        } else {
            std::print("  {}\r\n", options[i]);
        }
    }
}

// redraw_final reprints the list once the choice is made, marking the pick, so the
// terminal keeps a stable record after raw mode is restored.
void redraw_final(std::span<const std::string> options, std::size_t cursor) {
    for (std::size_t i = 0; i < options.size(); ++i) {
        if (i == cursor) {
            std::print("{}{}\r\n", color(bold(kGreen), "> "), color(bold(kGreen), options[i]));
        } else {
            std::print("  {}\r\n", color(kGrey, options[i]));
        }
    }
}

// select_interactive drives the raw-mode arrow-key menu, starting at cursor.
[[nodiscard]] std::expected<std::string, error::Error> select_interactive(
    std::string_view question, std::span<const std::string> options, std::size_t cursor) {
    std::print("{} {} {}\r\n", color(bold(kCyan), "?"), question,
               color(kGrey, "(up/down, enter)"));
    render(options, cursor);

    std::array<unsigned char, 3> buf{};
    while (true) {
        const auto got = ::read(STDIN_FILENO, buf.data(), buf.size());
        if (got <= 0) {
            return std::unexpected(error::abort_error());
        }
        const int n = static_cast<int>(got);
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
    std::println("{}", question);
    for (std::size_t i = 0; i < options.size(); ++i) {
        std::println("  {}) {}", i + 1, options[i]);
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
    if (!platform::is_tty(STDIN_FILENO)) {
        return detail::select_numbered(question, options);
    }
    auto raw = platform::enter_raw_mode(STDIN_FILENO);
    if (!raw) {
        return detail::select_numbered(question, options);
    }
    // The guard restores the terminal when it goes out of scope, on every path.
    return detail::select_interactive(question, options, detail::index_of(options, def));
}

}  // namespace cup::ui
