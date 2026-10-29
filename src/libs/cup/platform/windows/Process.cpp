module;
// WIN32_LEAN_AND_MEAN/NOMINMAX keep <windows.h> lean and its min/max macros
// out of the way of std::min/std::max.
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

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
module cup.platform.windows;

namespace cup::platform::windows {
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

}  // namespace

// quote_arg follows the rules CommandLineToArgvW (and every C runtime) uses
// to split a command line: Windows hands a process one flat string, not an
// argv array, so every argument must survive that round trip — spaces,
// embedded quotes and the backslashes in front of them alike.
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


namespace {

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

}  // namespace

std::expected<void, error::Error> run_command(const std::filesystem::path& dir,
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

// stdin/stderr stay inherited, as in the POSIX backend.
std::expected<std::string, error::Error> capture_command(const std::filesystem::path& dir,
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
