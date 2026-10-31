#pragma once

// The handful of POSIX calls the unit suites use to rig a test (a pipe on
// stdin, an environment variable, the process id) — <unistd.h> itself on
// POSIX, and thin shims over the Windows CRT's equivalents otherwise, so the
// same suites build and run on a Windows cup build too. Not a portability
// layer for cup itself: core owns that.

#ifdef _WIN32

#include <fcntl.h>
#include <io.h>
#include <process.h>

#include <cstddef>
#include <cstdlib>

#ifndef STDIN_FILENO
#define STDIN_FILENO 0
#endif
#ifndef STDOUT_FILENO
#define STDOUT_FILENO 1
#endif
#ifndef STDERR_FILENO
#define STDERR_FILENO 2
#endif

#ifdef _MSC_VER
using ssize_t = std::ptrdiff_t;
#endif

// Binary mode, so the bytes a test writes reach the reader unchanged (no
// "\n" -> "\r\n" translation).
inline int pipe(int fds[2]) { return ::_pipe(fds, 64 * 1024, _O_BINARY); }

// _putenv_s with an empty value removes the variable, which is exactly
// unsetenv; overwrite = 0 keeps an existing value, as POSIX setenv does.
inline int setenv(const char* name, const char* value, int overwrite) {
    if (overwrite == 0 && std::getenv(name) != nullptr) {
        return 0;
    }
    return ::_putenv_s(name, value);
}

inline int unsetenv(const char* name) { return ::_putenv_s(name, ""); }

#else

#include <unistd.h>

#endif

namespace cup::test {

// permission_bits_enforced reports whether chmod-style permission bits
// actually stop this process reading a file: not for root, which ignores
// them, and never on Windows, where perms::none only sets the read-only
// attribute. Suites that rely on an unreadable file SKIP when this is false.
inline bool permission_bits_enforced() {
#ifdef _WIN32
    return false;
#else
    return ::geteuid() != 0;
#endif
}

}
