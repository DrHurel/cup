module;
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
// <cstdlib> first, outside any extern "C": <windows.h> reaches it through
// the intrinsics headers from inside an extern "C" block, and GCC's modules
// then reject those declarations as conflicting with the C++-linkage ones
// cup.error's BMI already carries.
#include <cstdlib>
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include <array>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <expected>
#include <filesystem>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>
module cup.platform;

namespace cup::platform {
namespace {

std::string join(std::span<const std::string> args) {
    std::string out;
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (i > 0) {
            out += ' ';
        }
        out += args[i];
    }
    return out;
}

#ifdef _WIN32

// UTF-8 <-> UTF-16 at the Win32 boundary: cup's strings are UTF-8 throughout,
// while CreateProcessW and FormatMessageW speak UTF-16.
std::wstring widen(std::string_view s) {
    if (s.empty()) {
        return {};
    }
    const int n = ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring out(static_cast<std::size_t>(n), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n);
    return out;
}

std::string narrow(std::wstring_view s) {
    if (s.empty()) {
        return {};
    }
    const int n = ::WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr,
                                        0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(n), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n,
                          nullptr, nullptr);
    return out;
}

// win_error_message renders a GetLastError() code as the system's own text
// ("The system cannot find the file specified."), the Windows counterpart of
// the strerror(errno) the POSIX branch reports.
std::string win_error_message(DWORD code) {
    wchar_t* buf = nullptr;
    const DWORD len = ::FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, 0, reinterpret_cast<wchar_t*>(&buf), 0, nullptr);
    std::string out = len > 0 ? narrow(std::wstring_view(buf, len)) : std::string();
    ::LocalFree(buf);
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r' || out.back() == ' ' ||
                            out.back() == '.')) {
        out.pop_back();
    }
    return out.empty() ? std::format("error {}", static_cast<unsigned long>(code)) : out;
}

// quote_arg quotes one argument so the child's C runtime (CommandLineToArgvW
// rules) splits it back out unchanged: Windows hands a process one flat
// command line, not an argv array, so every argument must survive that round
// trip — spaces, embedded quotes and the backslashes in front of them alike.
std::wstring quote_arg(std::wstring_view arg) {
    if (!arg.empty() && arg.find_first_of(L" \t\n\v\"") == std::wstring_view::npos) {
        return std::wstring(arg);
    }
    std::wstring out = L"\"";
    for (auto it = arg.begin();; ++it) {
        std::size_t backslashes = 0;
        while (it != arg.end() && *it == L'\\') {
            ++it;
            ++backslashes;
        }
        if (it == arg.end()) {
            // Doubled so they escape themselves, not the closing quote.
            out.append(backslashes * 2, L'\\');
            break;
        }
        if (*it == L'"') {
            out.append(backslashes * 2 + 1, L'\\');
        } else {
            out.append(backslashes, L'\\');
        }
        out.push_back(*it);
    }
    out.push_back(L'"');
    return out;
}

std::wstring command_line(std::string_view name, std::span<const std::string> args) {
    std::wstring line = quote_arg(widen(name));
    for (const auto& arg : args) {
        line += L' ';
        line += quote_arg(widen(arg));
    }
    return line;
}

// The outcome of a CreateProcess/wait, before it's turned into cup's uniform
// "command failed: ..." error — the Windows shape of the POSIX branch's
// SpawnOutcome. output is only ever populated by a capture_output spawn.
struct SpawnOutcome {
    bool start_failed = false;
    DWORD start_error = 0;
    DWORD exit_code = 0;
    std::string output;
};

// Closes a Win32 handle on scope exit, so every early return below releases
// the pipe and process handles it holds.
struct Handle {
    HANDLE h = nullptr;
    Handle() = default;
    explicit Handle(HANDLE handle) : h(handle) {}
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    ~Handle() { reset(); }
    void reset() {
        if (h != nullptr && h != INVALID_HANDLE_VALUE) {
            ::CloseHandle(h);
        }
        h = nullptr;
    }
};

// Starts name with args from dir via CreateProcessW, capturing stdout into
// SpawnOutcome::output when capture_output is set (otherwise stdout is
// inherited). CreateProcessW resolves a bare name like "cmake" against PATH
// and appends ".exe" itself, so callers spell programs the same way on every
// platform. A missing program or working directory surfaces as start_failed
// (CreateProcessW itself fails), the counterpart of the POSIX branch's
// exec-failure pipe; anything after that is the program's own exit code.
std::expected<SpawnOutcome, error::Error> spawn_program(const std::filesystem::path& dir,
                                                         std::string_view name,
                                                         std::span<const std::string> args,
                                                         bool capture_output) {
    Handle out_read;
    Handle out_write;
    if (capture_output) {
        SECURITY_ATTRIBUTES sa{.nLength = sizeof(SECURITY_ATTRIBUTES),
                               .lpSecurityDescriptor = nullptr,
                               .bInheritHandle = TRUE};
        if (::CreatePipe(&out_read.h, &out_write.h, &sa, 0) == 0) {
            return std::unexpected(
                error::Error(std::format("CreatePipe: {}", win_error_message(::GetLastError()))));
        }
        // Only the write end goes to the child; an inherited read end would
        // keep the pipe open and the ReadFile loop below would never end.
        ::SetHandleInformation(out_read.h, HANDLE_FLAG_INHERIT, 0);
    }

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = ::GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = capture_output ? out_write.h : ::GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError = ::GetStdHandle(STD_ERROR_HANDLE);

    std::wstring cmd = command_line(name, args);
    const std::wstring cwd = dir.wstring();
    PROCESS_INFORMATION pi{};
    if (::CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, 0, nullptr, cwd.c_str(), &si,
                         &pi) == 0) {
        return SpawnOutcome{.start_failed = true, .start_error = ::GetLastError()};
    }
    Handle process(pi.hProcess);
    Handle thread(pi.hThread);
    // The child holds its own copy now; dropping the parent's lets ReadFile
    // see end-of-file once the child exits.
    out_write.reset();

    SpawnOutcome outcome;
    if (capture_output) {
        std::array<char, 4096> buf{};
        DWORD n = 0;
        while (::ReadFile(out_read.h, buf.data(), static_cast<DWORD>(buf.size()), &n, nullptr) != 0 &&
               n > 0) {
            outcome.output.append(buf.data(), n);
        }
    }

    if (::WaitForSingleObject(process.h, INFINITE) == WAIT_FAILED) {
        return std::unexpected(error::Error(
            std::format("WaitForSingleObject: {}", win_error_message(::GetLastError()))));
    }
    if (::GetExitCodeProcess(process.h, &outcome.exit_code) == 0) {
        return std::unexpected(error::Error(
            std::format("GetExitCodeProcess: {}", win_error_message(::GetLastError()))));
    }
    return outcome;
}

// Turns a spawn's outcome into cup's uniform "command failed: ..." error, or
// nullopt for a zero exit. A crashed process exits with its NTSTATUS
// (0xC0000005 for an access violation) — the closest thing Windows has to a
// terminating signal — which reads far better in hex.
std::optional<error::Error> exit_error(std::string_view name, std::string_view joined,
                                       const SpawnOutcome& outcome) {
    if (outcome.start_failed) {
        return error::Error(std::format("command failed: {} {}: {}", name, joined,
                                        win_error_message(outcome.start_error)));
    }
    if (outcome.exit_code == 0) {
        return std::nullopt;
    }
    if (outcome.exit_code >= 0xC0000000U) {
        return error::Error(std::format("command failed: {} {}: exit status 0x{:08X}", name, joined,
                                        static_cast<std::uint32_t>(outcome.exit_code)));
    }
    return error::Error(std::format("command failed: {} {}: exit status {}", name, joined,
                                    static_cast<std::uint32_t>(outcome.exit_code)));
}

#else

// Owns the argv storage: argv[i] points into storage[i], so the two travel
// together. Moving an Argv preserves those pointers -- a moved-from
// vector<string> transfers its heap buffer wholesale, it does not relocate
// the individual std::string objects argv points into.
struct Argv {
    std::vector<std::string> storage;
    std::vector<char*> argv;
};

Argv build_argv(std::string_view name, std::span<const std::string> args) {
    Argv a;
    a.storage.reserve(args.size() + 1);
    a.storage.emplace_back(name);
    for (const auto& arg : args) {
        a.storage.push_back(arg);
    }
    a.argv.reserve(a.storage.size() + 1);
    for (auto& arg : a.storage) {
        a.argv.push_back(arg.data());
    }
    a.argv.push_back(nullptr);
    return a;
}

// The outcome of a fork/exec/wait, before it's turned into cup's uniform
// "command failed: ..." error. output is only ever populated by a
// capture_output spawn.
struct SpawnOutcome {
    bool exec_failed = false;
    int child_errno = 0;
    int status = 0;
    std::string output;
};

// Forks and execs argv[0] with argv from dir, capturing stdout into
// SpawnOutcome::output when capture_output is set (otherwise stdout is
// inherited). Shared by run_command_impl and capture_command_impl: the only
// difference between them is this flag and what they do with the result.
//
// Uses fork/chdir/execvp/waitpid rather than posix_spawn: changing the
// child's working directory under posix_spawn needs the nonstandard
// posix_spawn_file_actions_addchdir_np extension, whose availability differs
// between glibc and musl (the Alpine release target). Plain fork+exec is
// POSIX and behaves identically under both. A self-pipe (CLOEXEC on the
// write end) lets the parent tell "chdir/exec failed" (errno arrives on the
// pipe before the child exits) apart from "the program ran and exited
// non-zero" (the pipe closes with no data once execvp succeeds).
std::expected<SpawnOutcome, error::Error> spawn(const std::filesystem::path& dir,
                                                 std::span<char* const> argv, bool capture_output) {
    std::array<int, 2> err_pipe{};
    if (::pipe(err_pipe.data()) != 0) {
        return std::unexpected(error::Error(std::format("pipe: {}", std::strerror(errno))));
    }
    ::fcntl(err_pipe[1], F_SETFD, FD_CLOEXEC);

    std::array<int, 2> out_pipe{-1, -1};
    if (capture_output && ::pipe(out_pipe.data()) != 0) {
        const int saved_errno = errno;
        ::close(err_pipe[0]);
        ::close(err_pipe[1]);
        return std::unexpected(error::Error(std::format("pipe: {}", std::strerror(saved_errno))));
    }

    const pid_t pid = ::fork();
    if (pid < 0) {
        const int saved_errno = errno;
        ::close(err_pipe[0]);
        ::close(err_pipe[1]);
        if (capture_output) {
            ::close(out_pipe[0]);
            ::close(out_pipe[1]);
        }
        return std::unexpected(error::Error(std::format("fork: {}", std::strerror(saved_errno))));
    }

    if (pid == 0) {
        ::close(err_pipe[0]);
        if (capture_output) {
            ::close(out_pipe[0]);
            // out_pipe[1] is always a real fd here: capture_output true with
            // an unfilled/failed out_pipe returns before fork() (line 89's
            // check), so this child only exists once that succeeded.
            if (out_pipe[1] >= 0) {
                ::dup2(out_pipe[1], STDOUT_FILENO);
            }
            ::close(out_pipe[1]);
        }
        if (::chdir(dir.c_str()) != 0) {
            const int child_errno = errno;
            static_cast<void>(::write(err_pipe[1], &child_errno, sizeof(child_errno)));
            _exit(127);
        }
        ::execvp(argv[0], argv.data());
        const int child_errno = errno;
        static_cast<void>(::write(err_pipe[1], &child_errno, sizeof(child_errno)));
        _exit(127);
    }

    ::close(err_pipe[1]);
    if (capture_output) {
        ::close(out_pipe[1]);
    }

    SpawnOutcome outcome;
    if (capture_output) {
        std::array<char, 4096> buf{};
        ssize_t n = 0;
        while ((n = ::read(out_pipe[0], buf.data(), buf.size())) > 0) {
            outcome.output.append(buf.data(), static_cast<std::size_t>(n));
        }
        ::close(out_pipe[0]);
    }

    const ssize_t read_bytes = ::read(err_pipe[0], &outcome.child_errno, sizeof(outcome.child_errno));
    ::close(err_pipe[0]);
    outcome.exec_failed = (read_bytes == sizeof(outcome.child_errno));

    if (::waitpid(pid, &outcome.status, 0) < 0) {
        return std::unexpected(error::Error(std::format("waitpid: {}", std::strerror(errno))));
    }
    return outcome;
}

// Turns a spawn's outcome into cup's uniform "command failed: ..." error, or
// nullopt for a zero exit.
std::optional<error::Error> exit_error(std::string_view name, std::string_view joined,
                                        const SpawnOutcome& outcome) {
    if (outcome.exec_failed) {
        return error::Error(std::format("command failed: {} {}: {}", name, joined,
                                        std::strerror(outcome.child_errno)));
    }
    if (WIFEXITED(outcome.status) && WEXITSTATUS(outcome.status) == 0) {
        return std::nullopt;
    }
    if (WIFEXITED(outcome.status)) {
        return error::Error(std::format("command failed: {} {}: exit status {}", name, joined,
                                        WEXITSTATUS(outcome.status)));
    }
    if (WIFSIGNALED(outcome.status)) {
        return error::Error(
            std::format("command failed: {} {}: signal {}", name, joined, WTERMSIG(outcome.status)));
    }
    return error::Error(std::format("command failed: {} {}: unknown exit status", name, joined));
}

std::expected<SpawnOutcome, error::Error> spawn_program(const std::filesystem::path& dir,
                                                         std::string_view name,
                                                         std::span<const std::string> args,
                                                         bool capture_output) {
    auto argv = build_argv(name, args);
    return spawn(dir, argv.argv, capture_output);
}

#endif

}

std::expected<void, error::Error> run_command_impl(const std::filesystem::path& dir,
                                                    std::string_view name,
                                                    std::span<const std::string> args) {
    auto outcome = spawn_program(dir, name, args, /*capture_output=*/false);
    if (!outcome.has_value()) {
        return std::unexpected(std::move(outcome).error());
    }
    if (auto err = exit_error(name, join(args), *outcome); err.has_value()) {
        return std::unexpected(*std::move(err));
    }
    return {};
}

// stdin/stderr stay inherited: stderr so a failure is still visible on the
// terminal, stdin because nothing capture_command is used for needs it.
std::expected<std::string, error::Error> capture_command_impl(const std::filesystem::path& dir,
                                                               std::string_view name,
                                                               std::span<const std::string> args) {
    auto outcome = spawn_program(dir, name, args, /*capture_output=*/true);
    if (!outcome.has_value()) {
        return std::unexpected(std::move(outcome).error());
    }
    if (auto err = exit_error(name, join(args), *outcome); err.has_value()) {
        return std::unexpected(*std::move(err));
    }
    return std::move(outcome->output);
}

}
