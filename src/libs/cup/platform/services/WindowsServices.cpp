// cup.platform's composition root on Windows: the one place the portable
// core (cup.platform) meets its Windows backend (cup.platform.windows).
// Compiled only on Windows — ../CMakeLists.txt picks this file or
// PosixServices.cpp — so nothing else in cup needs to know which OS it runs
// on, let alone #ifdef on it.

// Building the std::function entries below instantiates std::function's
// manager here, which needs <typeinfo> (typeid) visible in this TU itself —
// GCC doesn't take it from cup.platform's BMI.
#include <functional>
#include <typeinfo>

import cup.platform;
import cup.platform.windows;

namespace cup::platform::detail {

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
