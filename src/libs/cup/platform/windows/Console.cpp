module;
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
// <cstdlib> first: see Process.cpp.
#include <cstdlib>
#include <io.h>
#include <windows.h>

#include <expected>
#include <format>
#include <span>
module cup.platform.windows;

namespace cup::platform::windows {

bool is_tty(int fd) { return ::_isatty(fd) != 0; }

// The console equivalent of cfmakeraw: no line buffering, no echo, and
// Ctrl-C delivered as a key rather than a signal. VT input makes arrow keys
// arrive as the same escape sequences a POSIX terminal sends. _get_osfhandle
// maps the CRT descriptor onto the Win32 handle behind it; only a console
// input handle passes GetConsoleMode, so a pipe or file fails here the way
// tcgetattr fails off a terminal.
std::expected<RawMode, error::Error> enter_raw_mode(int fd) {
    const auto handle = reinterpret_cast<HANDLE>(::_get_osfhandle(fd));
    DWORD saved = 0;
    if (handle == INVALID_HANDLE_VALUE || ::GetConsoleMode(handle, &saved) == 0) {
        return std::unexpected(error::Error(
            std::format("GetConsoleMode: error {}", static_cast<unsigned long>(::GetLastError()))));
    }
    DWORD raw = saved;
    raw &= ~static_cast<DWORD>(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_PROCESSED_INPUT);
    raw |= ENABLE_VIRTUAL_TERMINAL_INPUT;
    if (::SetConsoleMode(handle, raw) == 0) {
        return std::unexpected(error::Error(
            std::format("SetConsoleMode: error {}", static_cast<unsigned long>(::GetLastError()))));
    }
    return RawMode::make_active([handle, saved] { ::SetConsoleMode(handle, saved); });
}

// UTF-8 output (cup prints "❯", "—", ...) and virtual-terminal processing so
// cup.ui's ANSI colour sequences render instead of printing as raw escapes.
// Failures are ignored: a redirected stdout has no console mode to set, and
// the output is still correct, just uncoloured.
void init_console() {
    ::SetConsoleOutputCP(CP_UTF8);
    for (const DWORD which : {STD_OUTPUT_HANDLE, STD_ERROR_HANDLE}) {
        const HANDLE handle = ::GetStdHandle(which);
        DWORD mode = 0;
        if (handle != INVALID_HANDLE_VALUE && ::GetConsoleMode(handle, &mode) != 0) {
            ::SetConsoleMode(handle, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
        }
    }
}

int read_input(int fd, std::span<unsigned char> buf) {
    return ::_read(fd, buf.data(), static_cast<unsigned int>(buf.size()));
}

}
