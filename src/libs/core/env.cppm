module;
#include <cstdlib>
#include <filesystem>
#include <string_view>
export module core:env;

import :services;

// Where a user's files live, how executables are named, and which shell to
// assume — the per-OS conventions logging (cup.log's directory), scaffold
// (the release cache) and cmd (completion install, `cup run`) rely on.
// The rules every platform shares live here; the OS-specific defaults come
// from the injected backend (see :services).
export namespace core {

namespace detail {

// env_path returns the variable's value as a path, or an empty path when it
// is unset or set to the empty string (which XDG treats the same as unset).
// Exported for the backends, which resolve their own defaults the same way.
[[nodiscard]] std::filesystem::path env_path(const char* name) {
    const char* value = std::getenv(name);
    return (value != nullptr && *value != '\0') ? std::filesystem::path(value)
                                                : std::filesystem::path();
}

}

// home_dir returns the user's home directory ($HOME; %USERPROFILE% too on
// Windows), empty when unknown.
[[nodiscard]] std::filesystem::path home_dir() { return services().home_dir(); }

// cache_dir returns cup's per-user cache directory: $XDG_CACHE_HOME/cup when
// set, on every platform, else the OS's own default (~/.cache/cup on POSIX,
// %LOCALAPPDATA%\cup on Windows).
[[nodiscard]] std::filesystem::path cache_dir() {
    if (auto xdg = detail::env_path("XDG_CACHE_HOME"); !xdg.empty()) {
        return xdg / "cup";
    }
    return services().native_cache_dir();
}

// exe_suffix is what the toolchain appends to an executable's name: a
// `cup run` of app "hello" runs build/<mode>/bin/hello.exe on Windows.
[[nodiscard]] std::string_view exe_suffix() { return services().exe_suffix; }

// default_shell is the shell to assume when $SHELL is unset: bash on POSIX,
// powershell on Windows (cmd and PowerShell set no $SHELL; MSYS2 does).
[[nodiscard]] std::string_view default_shell() { return services().default_shell; }

}
