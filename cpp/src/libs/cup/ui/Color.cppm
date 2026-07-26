module;
#include <cstdlib>
#include <string>
#include <string_view>
export module cup.ui:color;

import :io;
import cup.platform;

export namespace cup::ui {

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
    static bool enabled = std::getenv("NO_COLOR") == nullptr && platform::is_tty(platform::kStdoutFd);
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
    return std::string("\x{1b}[").append(code).append("m").append(s).append("\x{1b}[0m");
}

// bold builds the ";1" variant of a palette entry, used for the emphasised lines.
[[nodiscard]] std::string bold(std::string_view code) {
    return std::string(code) + ";1";
}

namespace detail {

// status prints one "  <label> <message>" line. Labels are padded to a common
// width by their callers so the messages line up in a column.
void status(std::string_view code, std::string_view label, std::string_view msg) {
    emit_line("  " + color(code, label) + " " + std::string(msg));
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
void accent(std::string_view msg) { emit_line(color(bold(kCyan), msg)); }
void success(std::string_view msg) { emit_line(color(bold(kGreen), msg)); }
void err(std::string_view msg) { emit_error_line(color(kRed, msg)); }

}  // namespace cup::ui
