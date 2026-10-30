module;
#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
export module posix;

export import core;

// The platform layer's POSIX backend (Linux, macOS, MSYS2's own runtime
// aside): the OS-facing half of every core::Services entry, built only on
// non-Windows hosts and injected into core by PosixServices.cpp. Declarations
// only where the definition needs OS headers (fork/exec, termios, libcurl) —
// those stay in this library's implementation units, out of the BMI.
//
// The namespace is posix_backend rather than the module's name: the standard
// reserves the top-level namespace posix for POSIX itself ([namespace.posix]).
namespace posix_backend {
// Not exported: this module's own units see these, importers don't.
namespace error = cup::error;
using core::RawMode;
}

export namespace posix_backend {

// fork/chdir/execvp/waitpid (Process.cpp).
[[nodiscard]] std::expected<void, error::Error> run_command(const std::filesystem::path& dir,
                                                            std::string_view name,
                                                            std::span<const std::string> args);
[[nodiscard]] std::expected<std::string, error::Error> capture_command(
    const std::filesystem::path& dir, std::string_view name, std::span<const std::string> args);

// libcurl (Http.cpp).
[[nodiscard]] std::expected<std::string, error::Error> http_get(std::string_view url);

// isatty/termios/read (Terminal.cpp).
[[nodiscard]] bool is_tty(int fd);
[[nodiscard]] std::expected<RawMode, error::Error> enter_raw_mode(int fd);
[[nodiscard]] int read_input(int fd, std::span<unsigned char> buf);

[[nodiscard]] std::filesystem::path home_dir() { return core::detail::env_path("HOME"); }

[[nodiscard]] std::filesystem::path native_cache_dir() { return home_dir() / ".cache" / "cup"; }

}

// The POSIX backend's Library. Nothing names it as a dependency — the backend is
// injected into core by PosixServices.cpp, never imported — but it declares
// its own graph like every other library.
export namespace posix_backend {

struct Library {
    using dependencies = std::tuple<core::Library>;
    static void initialize() {
        // Nothing to set up.
    }
    static void finalize() {
        // Nothing to tear down.
    }
};

}
