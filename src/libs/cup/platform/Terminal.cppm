module;
#ifdef _WIN32
// WIN32_LEAN_AND_MEAN/NOMINMAX keep <windows.h> from dragging winsock and the
// min/max macros into this partition's global module fragment, where they
// would collide with std::min/std::max in every importer's own code.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
// <cstdlib> first, outside any extern "C": <windows.h> reaches it through
// the intrinsics headers from inside an extern "C" block, and GCC's modules
// then reject those declarations as conflicting with the C++-linkage ones
// cup.error's BMI already carries.
#include <cstdlib>
#include <io.h>
#include <windows.h>
#else
#include <termios.h>
#include <unistd.h>
#endif

#include <cerrno>
#include <cstring>
#include <expected>
#include <format>
#include <string>
#include <utility>
export module cup.platform:terminal;

export import cup.error;

export namespace cup::platform {

// The standard three descriptors. Spelled as plain 0/1/2 rather than
// STDIN_FILENO & co., which <unistd.h> defines but the Windows CRT does not —
// the numbering itself is the same on both.
inline constexpr int kStdinFd = 0;
inline constexpr int kStdoutFd = 1;
inline constexpr int kStderrFd = 2;

#ifdef _WIN32
[[nodiscard]] bool is_tty(int fd) noexcept { return ::_isatty(fd) != 0; }
#else
[[nodiscard]] bool is_tty(int fd) noexcept { return ::isatty(fd) == 1; }
#endif

// init_console prepares the process's console for cup's output. On Windows
// that means switching the output code page to UTF-8 (cup prints "❯", "—",
// and other non-ASCII glyphs) and turning on virtual-terminal processing so
// the ANSI colour sequences cup.ui emits render instead of printing as raw
// escape codes. A POSIX terminal already does both, so it is a no-op there.
// Failures are ignored: a redirected stdout has no console mode to set, and
// cup's output is still correct (just uncoloured) without it.
void init_console() noexcept {
#ifdef _WIN32
    ::SetConsoleOutputCP(CP_UTF8);
    for (const DWORD which : {STD_OUTPUT_HANDLE, STD_ERROR_HANDLE}) {
        const HANDLE handle = ::GetStdHandle(which);
        DWORD mode = 0;
        if (handle != INVALID_HANDLE_VALUE && ::GetConsoleMode(handle, &mode) != 0) {
            ::SetConsoleMode(handle, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
        }
    }
#endif
}

#ifdef _WIN32
// The saved state RawMode restores: a console input handle and the mode it
// had before raw mode was entered — the Windows counterpart of a termios.
struct SavedTerminal {
    HANDLE handle = INVALID_HANDLE_VALUE;
    DWORD mode = 0;
};
#else
using SavedTerminal = termios;
#endif

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

    void restore() noexcept {
        if (active_) {
#ifdef _WIN32
            ::SetConsoleMode(saved_.handle, saved_.mode);
#else
            ::tcsetattr(fd_, TCSAFLUSH, &saved_);
#endif
            active_ = false;
        }
    }

    [[nodiscard]] bool owns() const noexcept { return active_; }

    [[nodiscard]] static RawMode make_active(int fd, const SavedTerminal& saved) {
        return RawMode(fd, saved);
    }

private:
    RawMode(int fd, const SavedTerminal& saved) : fd_(fd), saved_(saved), active_(true) {}

    int fd_ = -1;
    SavedTerminal saved_{};
    bool active_ = false;
};

#ifdef _WIN32
[[nodiscard]] std::expected<RawMode, cup::error::Error> enter_raw_mode(int fd) noexcept {
    // _get_osfhandle maps the CRT descriptor onto the Win32 handle behind it;
    // a console's input handle is the only kind GetConsoleMode accepts, so a
    // pipe or file fails here the way tcgetattr fails off a terminal.
    const auto handle = reinterpret_cast<HANDLE>(::_get_osfhandle(fd));
    DWORD saved = 0;
    if (handle == INVALID_HANDLE_VALUE || ::GetConsoleMode(handle, &saved) == 0) {
        return std::unexpected(cup::error::Error(
            std::format("GetConsoleMode: error {}", static_cast<unsigned long>(::GetLastError()))));
    }

    // The console equivalent of cfmakeraw: no line buffering, no echo, and
    // Ctrl-C delivered as a key rather than a signal. VT input makes arrow
    // keys arrive as the same escape sequences a POSIX terminal sends.
    DWORD raw = saved;
    raw &= ~static_cast<DWORD>(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_PROCESSED_INPUT);
    raw |= ENABLE_VIRTUAL_TERMINAL_INPUT;
    if (::SetConsoleMode(handle, raw) == 0) {
        return std::unexpected(cup::error::Error(
            std::format("SetConsoleMode: error {}", static_cast<unsigned long>(::GetLastError()))));
    }
    return RawMode::make_active(fd, SavedTerminal{.handle = handle, .mode = saved});
}
#else
[[nodiscard]] std::expected<RawMode, cup::error::Error> enter_raw_mode(int fd) noexcept {
    termios saved{};
    if (::tcgetattr(fd, &saved) != 0) {
        return std::unexpected(
            cup::error::Error(std::format("tcgetattr: {}", std::strerror(errno))));
    }

    termios raw = saved;
    raw.c_iflag &= ~static_cast<tcflag_t>(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR |
                                          IGNCR | ICRNL | IXON);
    raw.c_oflag &= ~static_cast<tcflag_t>(OPOST);
    raw.c_lflag &= ~static_cast<tcflag_t>(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
    raw.c_cflag &= ~static_cast<tcflag_t>(CSIZE | PARENB);
    raw.c_cflag |= static_cast<tcflag_t>(CS8);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;

    if (::tcsetattr(fd, TCSAFLUSH, &raw) != 0) {
        return std::unexpected(
            cup::error::Error(std::format("tcsetattr: {}", std::strerror(errno))));
    }
    return RawMode::make_active(fd, saved);
}
#endif

}
