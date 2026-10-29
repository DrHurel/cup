module;
#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
export module cup.platform.windows;

export import cup.platform;

// cup.platform's Windows backend: the OS-facing half of every Services entry,
// built only on Windows hosts and injected into the portable core by
// services/WindowsServices.cpp. Declarations only where the definition needs
// <windows.h> (CreateProcessW, WinHTTP, the console API) — those stay in this
// library's implementation units, out of the BMI.
export namespace cup::platform::windows {

// CreateProcessW with CommandLineToArgvW-correct quoting (Process.cpp).
[[nodiscard]] std::expected<void, error::Error> run_command(const std::filesystem::path& dir,
                                                            std::string_view name,
                                                            std::span<const std::string> args);
[[nodiscard]] std::expected<std::string, error::Error> capture_command(
    const std::filesystem::path& dir, std::string_view name, std::span<const std::string> args);

// quote_arg quotes one argument so the child's C runtime splits it back out
// unchanged (Process.cpp). Exposed for testing: it is the piece of
// run_command that has no POSIX counterpart at all.
[[nodiscard]] std::wstring quote_arg(std::wstring_view arg);

// WinHTTP (Http.cpp): the OS's own client and TLS stack, so cup.exe links no
// curl/OpenSSL.
[[nodiscard]] std::expected<std::string, error::Error> http_get(std::string_view url);

// The console API (Console.cpp).
[[nodiscard]] bool is_tty(int fd);
[[nodiscard]] std::expected<RawMode, error::Error> enter_raw_mode(int fd);
void init_console();
[[nodiscard]] int read_input(int fd, std::span<unsigned char> buf);

// A native Windows shell sets only %USERPROFILE%; MSYS2 and Git Bash set
// $HOME too, and win.
[[nodiscard]] std::filesystem::path home_dir() {
    auto home = cup::platform::detail::env_path("HOME");
    return home.empty() ? cup::platform::detail::env_path("USERPROFILE") : home;
}

// %LOCALAPPDATA%\cup — where per-user, per-machine caches belong on Windows —
// else ~/.cache/cup as on POSIX.
[[nodiscard]] std::filesystem::path native_cache_dir() {
    if (auto local = cup::platform::detail::env_path("LOCALAPPDATA"); !local.empty()) {
        return local / "cup";
    }
    return home_dir() / ".cache" / "cup";
}

}
