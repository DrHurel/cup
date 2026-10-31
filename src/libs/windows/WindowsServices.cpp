// The platform layer's composition root on Windows: the one place the
// portable module core meets its Windows backend (module windows). Compiled
// only on Windows — ../CMakeLists.txt adds this directory or ../posix, never
// both — so nothing else in cup needs to know which OS it runs on, let alone
// #ifdef on it.

// Building the std::function entries below instantiates std::function's
// manager here, which needs <typeinfo> (typeid) visible in this TU itself —
// GCC doesn't take it from core's BMI.
#include <functional>
#include <typeinfo>

import core;
import windows;

namespace core::detail {

Services make_platform_services() {
    return Services{
        .run_command = &windows::run_command,
        .capture_command = &windows::capture_command,
        .http_get = &windows::http_get,
        .is_tty = &windows::is_tty,
        .enter_raw_mode = &windows::enter_raw_mode,
        .init_console = &windows::init_console,
        .read_input = &windows::read_input,
        .home_dir = &windows::home_dir,
        .native_cache_dir = &windows::native_cache_dir,
        .exe_suffix = ".exe",
        // cmd and PowerShell set no $SHELL; MSYS2 and Git Bash do.
        .default_shell = "powershell",
    };
}

}
