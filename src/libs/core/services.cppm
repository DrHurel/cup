module;
#include <expected>
#include <filesystem>
#include <functional>
#include <span>
#include <string>
#include <string_view>
export module core:services;

export import cup.error;
import :raw_mode;

// The platform layer's dependency-injection seam. The portable core declares
// *what* cup needs from the operating system — this Services table — and never
// how: every OS-specific implementation lives in a backend library
// (src/libs/posix, src/libs/windows), and that backend's services file
// (PosixServices.cpp, WindowsServices.cpp) injects it into core by defining
// make_platform_services(). CMake builds exactly one backend and its services
// file for the target platform (see src/libs/CMakeLists.txt), so no
// #ifdef _WIN32 is needed anywhere in cup.

// core sits outside namespace cup, so it names cup.error through this alias
// (not exported: core's own partitions see it, importers don't).
namespace core {
namespace error = cup::error;
}

export namespace core {

using RunCommandFunc = std::function<std::expected<void, error::Error>(
    const std::filesystem::path&, std::string_view, std::span<const std::string>)>;

using CaptureCommandFunc = std::function<std::expected<std::string, error::Error>(
    const std::filesystem::path&, std::string_view, std::span<const std::string>)>;

using HttpGetFunc = std::function<std::expected<std::string, error::Error>(std::string_view)>;

struct Services {
    // --- processes (:process) ---------------------------------------------
    // Run a program from a directory with inherited stdio / with its stdout
    // captured; an error if it can't start or exits non-zero.
    RunCommandFunc run_command;
    CaptureCommandFunc capture_command;

    // --- network (:net) ---------------------------------------------------
    // GET a URL: cup's User-Agent, a short timeout, http/https only, TLS 1.2
    // floor, body capped; anything but a 200 is an error.
    HttpGetFunc http_get;

    // --- terminal (:terminal) ---------------------------------------------
    std::function<bool(int fd)> is_tty;
    std::function<std::expected<RawMode, error::Error>(int fd)> enter_raw_mode;
    // Prepare the console for cup's output (UTF-8, ANSI colours) — a no-op
    // where the terminal already does both.
    std::function<void()> init_console;
    // Unbuffered read of up to buf.size() bytes from fd; <= 0 at EOF/error.
    std::function<int(int fd, std::span<unsigned char> buf)> read_input;

    // --- environment (:env) -----------------------------------------------
    // The user's home directory, empty if unknown.
    std::function<std::filesystem::path()> home_dir;
    // The OS's own per-user cache directory for cup, used when
    // XDG_CACHE_HOME (honoured on every platform, by :env) is unset.
    std::function<std::filesystem::path()> native_cache_dir;
    // What the toolchain appends to an executable's name (".exe" or "").
    std::string exe_suffix;
    // The shell to assume when $SHELL says nothing (completion install).
    std::string default_shell;
};

}

// The injection point: declared here, defined by whichever services file
// CMake built for this platform. extern "C++" attaches it to the global
// module rather than core, which is what lets a TU outside this module
// (the services file, which also imports the backend) provide the definition.
export extern "C++" {
namespace core::detail {
[[nodiscard]] Services make_platform_services();
}
}

export namespace core {

// services is the live table every core call goes through, built on
// first use from make_platform_services(). Returned by reference so tests can
// swap one entry (see run_command_func()) without touching the rest.
[[nodiscard]] Services& services() {
    static Services s = detail::make_platform_services();
    return s;
}

}
