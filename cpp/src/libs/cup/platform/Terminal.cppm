module;
#include <termios.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <expected>
#include <format>
#include <string>
#include <utility>
export module cup.platform:terminal;

export import cup.error;

export namespace cup::platform {

inline constexpr int kStdinFd = STDIN_FILENO;
inline constexpr int kStdoutFd = STDOUT_FILENO;
inline constexpr int kStderrFd = STDERR_FILENO;

[[nodiscard]] bool is_tty(int fd) noexcept { return ::isatty(fd) == 1; }

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
            ::tcsetattr(fd_, TCSAFLUSH, &saved_);
            active_ = false;
        }
    }

    [[nodiscard]] bool owns() const noexcept { return active_; }

    [[nodiscard]] static RawMode make_active(int fd, const termios& saved) {
        return RawMode(fd, saved);
    }

private:
    RawMode(int fd, const termios& saved) : fd_(fd), saved_(saved), active_(true) {}

    int fd_ = -1;
    termios saved_{};
    bool active_ = false;
};

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

}
