module;
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <cerrno>
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
            ::dup2(out_pipe[1], STDOUT_FILENO);
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

}

std::expected<void, error::Error> run_command_impl(const std::filesystem::path& dir,
                                                    std::string_view name,
                                                    std::span<const std::string> args) {
    auto argv = build_argv(name, args);
    auto outcome = spawn(dir, argv.argv, /*capture_output=*/false);
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
    auto argv = build_argv(name, args);
    auto outcome = spawn(dir, argv.argv, /*capture_output=*/true);
    if (!outcome.has_value()) {
        return std::unexpected(std::move(outcome).error());
    }
    if (auto err = exit_error(name, join(args), *outcome); err.has_value()) {
        return std::unexpected(*std::move(err));
    }
    return std::move(outcome->output);
}

}
