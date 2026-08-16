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

}

// Uses fork/chdir/execvp/waitpid rather than posix_spawn: changing the
// child's working directory under posix_spawn needs the nonstandard
// posix_spawn_file_actions_addchdir_np extension, whose availability differs
// between glibc and musl (the Alpine release target). Plain fork+exec is
// POSIX and behaves identically under both. A self-pipe (CLOEXEC on the
// write end) lets the parent tell "chdir/exec failed" (errno arrives on the
// pipe before the child exits) apart from "the program ran and exited
// non-zero" (the pipe closes with no data once execvp succeeds).
std::expected<void, error::Error> run_command_impl(const std::filesystem::path& dir,
                                                    std::string_view name,
                                                    std::span<const std::string> args) {
    std::vector<std::string> argv_storage;
    argv_storage.reserve(args.size() + 1);
    argv_storage.emplace_back(name);
    for (const auto& arg : args) {
        argv_storage.push_back(arg);
    }

    std::vector<char*> argv;
    argv.reserve(argv_storage.size() + 1);
    for (auto& arg : argv_storage) {
        argv.push_back(arg.data());
    }
    argv.push_back(nullptr);

    std::array<int, 2> pipefd{};
    if (::pipe(pipefd.data()) != 0) {
        return std::unexpected(error::Error(std::format("pipe: {}", std::strerror(errno))));
    }
    ::fcntl(pipefd[1], F_SETFD, FD_CLOEXEC);

    const pid_t pid = ::fork();
    if (pid < 0) {
        const int saved_errno = errno;
        ::close(pipefd[0]);
        ::close(pipefd[1]);
        return std::unexpected(error::Error(std::format("fork: {}", std::strerror(saved_errno))));
    }

    if (pid == 0) {
        ::close(pipefd[0]);
        if (::chdir(dir.c_str()) != 0) {
            const int child_errno = errno;
            static_cast<void>(::write(pipefd[1], &child_errno, sizeof(child_errno)));
            _exit(127);
        }
        ::execvp(argv[0], argv.data());
        const int child_errno = errno;
        static_cast<void>(::write(pipefd[1], &child_errno, sizeof(child_errno)));
        _exit(127);
    }

    ::close(pipefd[1]);
    int child_errno = 0;
    const ssize_t read_bytes = ::read(pipefd[0], &child_errno, sizeof(child_errno));
    ::close(pipefd[0]);

    int status = 0;
    if (::waitpid(pid, &status, 0) < 0) {
        return std::unexpected(error::Error(std::format("waitpid: {}", std::strerror(errno))));
    }

    const std::string joined = join(args);

    if (read_bytes == sizeof(child_errno)) {
        return std::unexpected(error::Error(
            std::format("command failed: {} {}: {}", name, joined, std::strerror(child_errno))));
    }
    if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
        return {};
    }
    if (WIFEXITED(status)) {
        return std::unexpected(error::Error(std::format(
            "command failed: {} {}: exit status {}", name, joined, WEXITSTATUS(status))));
    }
    if (WIFSIGNALED(status)) {
        return std::unexpected(error::Error(std::format(
            "command failed: {} {}: signal {}", name, joined, WTERMSIG(status))));
    }
    return std::unexpected(
        error::Error(std::format("command failed: {} {}: unknown exit status", name, joined)));
}

// Same fork/exec shape as run_command_impl, plus a second (non-CLOEXEC) pipe
// dup2'd onto the child's stdout so the parent can read it back. stdin/stderr
// stay inherited: stderr so a failure is still visible on the terminal,
// stdin because nothing capture_command is used for needs it.
std::expected<std::string, error::Error> capture_command_impl(const std::filesystem::path& dir,
                                                               std::string_view name,
                                                               std::span<const std::string> args) {
    std::vector<std::string> argv_storage;
    argv_storage.reserve(args.size() + 1);
    argv_storage.emplace_back(name);
    for (const auto& arg : args) {
        argv_storage.push_back(arg);
    }

    std::vector<char*> argv;
    argv.reserve(argv_storage.size() + 1);
    for (auto& arg : argv_storage) {
        argv.push_back(arg.data());
    }
    argv.push_back(nullptr);

    std::array<int, 2> err_pipe{};
    if (::pipe(err_pipe.data()) != 0) {
        return std::unexpected(error::Error(std::format("pipe: {}", std::strerror(errno))));
    }
    ::fcntl(err_pipe[1], F_SETFD, FD_CLOEXEC);

    std::array<int, 2> out_pipe{};
    if (::pipe(out_pipe.data()) != 0) {
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
        ::close(out_pipe[0]);
        ::close(out_pipe[1]);
        return std::unexpected(error::Error(std::format("fork: {}", std::strerror(saved_errno))));
    }

    if (pid == 0) {
        ::close(err_pipe[0]);
        ::close(out_pipe[0]);
        ::dup2(out_pipe[1], STDOUT_FILENO);
        ::close(out_pipe[1]);
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
    ::close(out_pipe[1]);

    std::string output;
    std::array<char, 4096> buf{};
    ssize_t n = 0;
    while ((n = ::read(out_pipe[0], buf.data(), buf.size())) > 0) {
        output.append(buf.data(), static_cast<std::size_t>(n));
    }
    ::close(out_pipe[0]);

    int child_errno = 0;
    const ssize_t read_bytes = ::read(err_pipe[0], &child_errno, sizeof(child_errno));
    ::close(err_pipe[0]);

    int status = 0;
    if (::waitpid(pid, &status, 0) < 0) {
        return std::unexpected(error::Error(std::format("waitpid: {}", std::strerror(errno))));
    }

    const std::string joined = join(args);

    if (read_bytes == sizeof(child_errno)) {
        return std::unexpected(error::Error(
            std::format("command failed: {} {}: {}", name, joined, std::strerror(child_errno))));
    }
    if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
        return output;
    }
    if (WIFEXITED(status)) {
        return std::unexpected(error::Error(std::format(
            "command failed: {} {}: exit status {}", name, joined, WEXITSTATUS(status))));
    }
    if (WIFSIGNALED(status)) {
        return std::unexpected(error::Error(std::format(
            "command failed: {} {}: signal {}", name, joined, WTERMSIG(status))));
    }
    return std::unexpected(
        error::Error(std::format("command failed: {} {}: unknown exit status", name, joined)));
}

}
