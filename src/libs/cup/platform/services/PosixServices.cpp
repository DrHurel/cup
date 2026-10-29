// cup.platform's composition root on POSIX hosts: the one place the portable
// core (cup.platform) meets its POSIX backend (cup.platform.posix). Compiled
// only on non-Windows hosts — ../CMakeLists.txt picks this file or
// WindowsServices.cpp — so nothing else in cup needs to know which OS it runs
// on, let alone #ifdef on it.

// Building the std::function entries below instantiates std::function's
// manager here, which needs <typeinfo> (typeid) visible in this TU itself —
// GCC doesn't take it from cup.platform's BMI.
#include <functional>
#include <typeinfo>

import cup.platform;
import cup.platform.posix;

namespace cup::platform::detail {

Services make_platform_services() {
    return Services{
        .run_command = &posix::run_command,
        .capture_command = &posix::capture_command,
        .http_get = &posix::http_get,
        .is_tty = &posix::is_tty,
        .enter_raw_mode = &posix::enter_raw_mode,
        // A POSIX terminal already takes UTF-8 and ANSI colours.
        .init_console = [] {},
        .read_input = &posix::read_input,
        .home_dir = &posix::home_dir,
        .native_cache_dir = &posix::native_cache_dir,
        .exe_suffix = "",
        .default_shell = "bash",
    };
}

}
