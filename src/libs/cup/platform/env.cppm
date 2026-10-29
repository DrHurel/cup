module;
#include <cstdlib>
#include <filesystem>
#include <string_view>
export module cup.platform:env;

// Where a user's files live, and how executables are named — the per-OS
// conventions cup.log (cup.log's directory), cup.scaffold (the release
// cache) and cup.cmd (completion install, `cup run`) each used to spell out
// for POSIX alone.
export namespace cup::platform {

// kExeSuffix is what the toolchain appends to an executable's name: a
// `cup run` of app "hello" runs build/<mode>/bin/hello.exe on Windows.
#ifdef _WIN32
inline constexpr std::string_view kExeSuffix = ".exe";
#else
inline constexpr std::string_view kExeSuffix = "";
#endif

namespace detail {

// env_path returns the variable's value as a path, or an empty path when it
// is unset or set to the empty string (which XDG treats the same as unset).
[[nodiscard]] std::filesystem::path env_path(const char* name) {
    const char* value = std::getenv(name);
    return (value != nullptr && *value != '\0') ? std::filesystem::path(value)
                                                : std::filesystem::path();
}

}

// home_dir returns $HOME, falling back on Windows to %USERPROFILE% — a
// native Windows shell sets only the latter, while MSYS2/Git Bash set both.
// Empty when neither is set.
[[nodiscard]] std::filesystem::path home_dir() {
    auto home = detail::env_path("HOME");
#ifdef _WIN32
    if (home.empty()) {
        home = detail::env_path("USERPROFILE");
    }
#endif
    return home;
}

// cache_dir returns cup's per-user cache directory: $XDG_CACHE_HOME/cup when
// set, else %LOCALAPPDATA%/cup on Windows (where per-machine caches belong
// there), else ~/.cache/cup.
[[nodiscard]] std::filesystem::path cache_dir() {
    if (auto xdg = detail::env_path("XDG_CACHE_HOME"); !xdg.empty()) {
        return xdg / "cup";
    }
#ifdef _WIN32
    if (auto local = detail::env_path("LOCALAPPDATA"); !local.empty()) {
        return local / "cup";
    }
#endif
    return home_dir() / ".cache" / "cup";
}

}
