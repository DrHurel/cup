module;
#include <termios.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <expected>
#include <format>
#include <span>
module cup.platform.posix;

namespace cup::platform::posix {

bool is_tty(int fd) { return ::isatty(fd) == 1; }

std::expected<RawMode, error::Error> enter_raw_mode(int fd) {
    termios saved{};
    if (::tcgetattr(fd, &saved) != 0) {
        return std::unexpected(error::Error(std::format("tcgetattr: {}", std::strerror(errno))));
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
        return std::unexpected(error::Error(std::format("tcsetattr: {}", std::strerror(errno))));
    }
    return RawMode::make_active([fd, saved] { ::tcsetattr(fd, TCSAFLUSH, &saved); });
}

int read_input(int fd, std::span<unsigned char> buf) {
    return static_cast<int>(::read(fd, buf.data(), buf.size()));
}

}
