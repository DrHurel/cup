module;
#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
export module windows;

export import core;

// The platform layer's Windows backend: the OS-facing half of every
// core::Services entry, built only on Windows hosts and injected into core by
// WindowsServices.cpp. Declarations only where the definition needs
// <windows.h> (CreateProcessW, WinHTTP, the console API) — those stay in this
// library's implementation units, out of the BMI.
namespace windows {
// Not exported: this module's own units see these, importers don't.
using core::RawMode;
}

export namespace windows {

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
    auto home = core::detail::env_path("HOME");
    return home.empty() ? core::detail::env_path("USERPROFILE") : home;
}

// %LOCALAPPDATA%\cup — where per-user, per-machine caches belong on Windows —
// else ~/.cache/cup as on POSIX.
[[nodiscard]] std::filesystem::path native_cache_dir() {
    if (auto local = core::detail::env_path("LOCALAPPDATA"); !local.empty()) {
        return local / "cup";
    }
    return home_dir() / ".cache" / "cup";
}

}

// The Windows backend's Library. Nothing names it as a dependency — the backend
// is injected into core by WindowsServices.cpp, never imported — but it
// declares its own graph like every other library.
export namespace windows {

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
