module;
#include <expected>
#include <span>
export module cup.platform:terminal;

export import cup.error;
export import :raw_mode;
import :services;

// Terminal primitives, forwarded to the injected backend (see :services).
export namespace cup::platform {

// The standard three descriptors — the same 0/1/2 on POSIX and Windows.
inline constexpr int kStdinFd = 0;
inline constexpr int kStdoutFd = 1;
inline constexpr int kStderrFd = 2;

[[nodiscard]] bool is_tty(int fd) { return services().is_tty(fd); }

// enter_raw_mode switches fd's terminal to raw input (no line buffering, no
// echo, Ctrl-C as a key) until the returned guard is restored or destroyed;
// an error off a terminal.
[[nodiscard]] std::expected<RawMode, error::Error> enter_raw_mode(int fd) {
    return services().enter_raw_mode(fd);
}

// init_console prepares the console for cup's output — see Services.
void init_console() { services().init_console(); }

// read_input is an unbuffered read of up to buf.size() bytes from fd.
[[nodiscard]] int read_input(int fd, std::span<unsigned char> buf) {
    return services().read_input(fd, buf);
}

}
