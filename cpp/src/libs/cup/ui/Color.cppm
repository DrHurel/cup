module;
#include <cstdlib>
#include <string>
#include <string_view>
export module cup.ui:color;

import :io;
import cup.platform;

export namespace cup::ui {

inline constexpr std::string_view kCyan = "38;5;81";
inline constexpr std::string_view kGreen = "38;5;77";
inline constexpr std::string_view kOrange = "38;5;215";
inline constexpr std::string_view kGrey = "38;5;244";
inline constexpr std::string_view kRed = "38;5;203;1";
inline constexpr std::string_view kSalmon = "38;5;209";

namespace detail {

bool& colour_enabled() {
    static bool enabled = std::getenv("NO_COLOR") == nullptr && platform::is_tty(platform::kStdoutFd);
    return enabled;
}

}

[[nodiscard]] bool use_color() { return detail::colour_enabled(); }

bool set_use_color(bool enabled) {
    const bool previous = detail::colour_enabled();
    detail::colour_enabled() = enabled;
    return previous;
}

[[nodiscard]] std::string color(std::string_view code, std::string_view s) {
    if (!use_color()) {
        return std::string(s);
    }
    return format_text("\x{1b}[{}m{}\x{1b}[0m", code, s);
}

[[nodiscard]] std::string bold(std::string_view code) {
    return format_text("{};1", code);
}

namespace detail {

void status(std::string_view code, std::string_view label, std::string_view msg) {
    emit_line(format_text("  {} {}", color(code, label), msg));
}

}

void running(std::string_view msg) { detail::status(kCyan, "run     ", msg); }
void wrote(std::string_view msg) { detail::status(kGreen, "wrote   ", msg); }
void updated(std::string_view msg) { detail::status(kOrange, "updated ", msg); }
void skipped(std::string_view msg) { detail::status(kGrey, "skipped ", msg); }
void removed(std::string_view msg) { detail::status(kSalmon, "removed ", msg); }
void next(std::string_view msg) { detail::status(kCyan, "next    ", msg); }

void accent(std::string_view msg) { emit_line(color(bold(kCyan), msg)); }
void success(std::string_view msg) { emit_line(color(bold(kGreen), msg)); }
void err(std::string_view msg) { emit_error_line(color(kRed, msg)); }

}
