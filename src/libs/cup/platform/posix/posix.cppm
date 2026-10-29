module;
#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
export module cup.platform.posix;

export import cup.platform;

// cup.platform's POSIX backend (Linux, macOS, MSYS2's own runtime aside): the
// OS-facing half of every Services entry, built only on non-Windows hosts and
// injected into the portable core by services/PosixServices.cpp. Declarations
// only where the definition needs OS headers (fork/exec, termios, libcurl) —
// those stay in this library's implementation units, out of the BMI.
export namespace cup::platform::posix {

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

[[nodiscard]] std::filesystem::path home_dir() { return cup::platform::detail::env_path("HOME"); }

[[nodiscard]] std::filesystem::path native_cache_dir() { return home_dir() / ".cache" / "cup"; }

}
