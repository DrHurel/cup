// The platform layer's composition root on POSIX hosts: the one place the
// portable module core meets its POSIX backend (module posix). Compiled only
// on non-Windows hosts — ../CMakeLists.txt adds this directory or ../windows,
// never both — so nothing else in cup needs to know which OS it runs on, let
// alone #ifdef on it.

// Building the std::function entries below instantiates std::function's
// manager here, which needs <typeinfo> (typeid) visible in this TU itself —
// GCC doesn't take it from core's BMI.
#include <functional>
#include <typeinfo>

import core;
import posix;

namespace core::detail {

Services make_platform_services() {
    return Services{
        .run_command = &posix_backend::run_command,
        .capture_command = &posix_backend::capture_command,
        .http_get = &posix_backend::http_get,
        .is_tty = &posix_backend::is_tty,
        .enter_raw_mode = &posix_backend::enter_raw_mode,
        // A POSIX terminal already takes UTF-8 and ANSI colours.
        .init_console = [] {},
        .read_input = &posix_backend::read_input,
        .home_dir = &posix_backend::home_dir,
        .native_cache_dir = &posix_backend::native_cache_dir,
        .exe_suffix = "",
        .default_shell = "bash",
    };
}

}
