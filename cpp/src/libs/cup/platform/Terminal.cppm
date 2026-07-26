module;
#include <termios.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <expected>
#include <string>
#include <utility>
export module cup.platform:terminal;

// Re-exported, not merely imported: cup::error::Error appears in this partition's
// exported signatures (std::expected<RawMode, Error>), so it is part of the
// interface consumers see through cup.platform.
export import cup.error;

export namespace cup::platform {

// The standard descriptors, re-exported as plain constants so callers can ask
// about a terminal without including <unistd.h> themselves. Keeping the POSIX
// headers behind this module is the whole point of the platform seam. POSIX fixes
// these values, so the constants cannot drift from the macros.
inline constexpr int kStdinFd = STDIN_FILENO;
inline constexpr int kStdoutFd = STDOUT_FILENO;
inline constexpr int kStderrFd = STDERR_FILENO;

// is_tty reports whether fd refers to a terminal. Every prompt in cup.ui asks this
// before attempting raw mode and falls back to a pipe-friendly path when it is
// false — which is also what makes the prompts scriptable from tests.
// (Go: term.IsTerminal.)
[[nodiscard]] bool is_tty(int fd) noexcept { return ::isatty(fd) == 1; }

// RawMode owns a terminal's original settings and restores them on destruction, so
// an early return or a thrown exception can never leave the user's shell in raw
// mode. It is the RAII form of Go's `defer term.Restore(fd, oldState)`.
class RawMode {
public:
    RawMode(const RawMode&) = delete;
    RawMode& operator=(const RawMode&) = delete;

    RawMode(RawMode&& other) noexcept
        : fd_(other.fd_), saved_(other.saved_),
          active_(std::exchange(other.active_, false)) {}

    RawMode& operator=(RawMode&& other) noexcept {
        if (this != &other) {
            restore();
            fd_ = other.fd_;
            saved_ = other.saved_;
            active_ = std::exchange(other.active_, false);
        }
        return *this;
    }

    ~RawMode() { restore(); }

    // restore puts the terminal back as it was. Safe to call more than once; the
    // destructor calls it too.
    void restore() noexcept {
        if (active_) {
            ::tcsetattr(fd_, TCSAFLUSH, &saved_);
            active_ = false;
        }
    }

    // owns reports whether this guard still holds settings to restore — false once
    // it has been moved from or restored.
    [[nodiscard]] bool owns() const noexcept { return active_; }

    // make_active is the factory enter_raw_mode uses. It is public only because a
    // std::expected must be able to construct the value in place.
    [[nodiscard]] static RawMode make_active(int fd, const termios& saved) {
        return RawMode(fd, saved);
    }

private:
    RawMode(int fd, const termios& saved) : fd_(fd), saved_(saved), active_(true) {}

    int fd_ = -1;
    termios saved_{};
    bool active_ = false;
};

// enter_raw_mode puts fd into raw mode — no echo, no line buffering, so a single
// keypress is readable — and returns a guard restoring the previous settings.
// (Go: term.MakeRaw.)
//
// Callers treat failure as "this is not an interactive terminal" and fall back to
// the numbered prompt, exactly as the Go implementation does.
[[nodiscard]] std::expected<RawMode, cup::error::Error> enter_raw_mode(int fd) noexcept {
    termios saved{};
    if (::tcgetattr(fd, &saved) != 0) {
        return std::unexpected(
            cup::error::Error(std::string("tcgetattr: ") + std::strerror(errno)));
    }

    termios raw = saved;
    // Mirror cfmakeraw's flag handling, which is what golang.org/x/term's MakeRaw
    // does, so key decoding behaves identically to the Go implementation.
    raw.c_iflag &= ~static_cast<tcflag_t>(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR |
                                          IGNCR | ICRNL | IXON);
    raw.c_oflag &= ~static_cast<tcflag_t>(OPOST);
    raw.c_lflag &= ~static_cast<tcflag_t>(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
    raw.c_cflag &= ~static_cast<tcflag_t>(CSIZE | PARENB);
    raw.c_cflag |= static_cast<tcflag_t>(CS8);
    // Block until at least one byte is available, with no inter-byte timer.
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;

    if (::tcsetattr(fd, TCSAFLUSH, &raw) != 0) {
        return std::unexpected(
            cup::error::Error(std::string("tcsetattr: ") + std::strerror(errno)));
    }
    return RawMode::make_active(fd, saved);
}

}  // namespace cup::platform
