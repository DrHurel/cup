#include <catch2/catch_test_macros.hpp>

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <thread>

#include "TempDir.hpp"

#ifndef CUP_TEST_BIN
#error "CUP_TEST_BIN must be defined to the path of the built cup binary"
#endif

// The one test in the suite that drives the *real* cup binary through a
// *real* pty, end to end -- every other interactive test either feeds
// synthetic ftxui::Event objects straight to a component (ui_test.cpp) or
// exercises the non-tty fallback via a piped stdin (ScopedStdin elsewhere).
// Neither path ever sends a raw byte through a pty into
// ftxui::ScreenInteractive::Loop(), which is exactly the code path
// ScopedSigintWatcher (Interactive.cpp) exists to guard: this is the only
// place that can prove Ctrl-C actually aborts gracefully instead of killing
// the process outright.

using cup::test::TempDir;

namespace {

class Pty {
public:
    Pty() {
        master_ = ::posix_openpt(O_RDWR | O_NOCTTY);
        REQUIRE(master_ >= 0);
        REQUIRE(::grantpt(master_) == 0);
        REQUIRE(::unlockpt(master_) == 0);
        const char* const name = ::ptsname(master_);
        REQUIRE(name != nullptr);
        slave_path_ = name;
    }

    Pty(const Pty&) = delete;
    Pty& operator=(const Pty&) = delete;
    ~Pty() { ::close(master_); }

    [[nodiscard]] int master() const { return master_; }
    [[nodiscard]] const std::string& slave_path() const { return slave_path_; }

private:
    int master_ = -1;
    std::string slave_path_;
};

// Reads from fd, accumulating, until `needle` appears or `timeout` elapses.
// Returns whatever was accumulated either way, so a failing REQUIRE's INFO
// can show what the child actually printed.
std::string read_until(int fd, std::string_view needle, std::chrono::milliseconds timeout) {
    std::string buf;
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now());
        pollfd pfd{.fd = fd, .events = POLLIN, .revents = 0};
        const int rc = ::poll(&pfd, 1, remaining.count() > 0 ? static_cast<int>(remaining.count()) : 0);
        if (rc <= 0) {
            continue;
        }
        char chunk[256];
        const ssize_t n = ::read(fd, chunk, sizeof(chunk));
        if (n <= 0) {
            break;
        }
        buf.append(chunk, static_cast<std::size_t>(n));
        if (buf.find(needle) != std::string::npos) {
            return buf;
        }
    }
    return buf;
}

// A bounded waitpid: polls with WNOHANG instead of blocking forever, so a
// regression to a hang fails the test instead of wedging the whole suite.
bool wait_for_exit(pid_t pid, int& status, std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (::waitpid(pid, &status, WNOHANG) == pid) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}

// Forks and execs the real cup binary from an empty cwd with `new demo`, so
// it lands directly on the first interactive prompt (build system?) without
// needing to answer a name prompt first. The child becomes its own session
// leader and then opens the pty slave by path -- with no controlling
// terminal yet, that open() is what makes the pty this process's controlling
// terminal, which is the mechanism that turns a raw ^C byte into a real
// SIGINT delivered by the kernel's line discipline (as opposed to just an
// input byte FTXUI would otherwise have to interpret itself).
pid_t spawn_cup_new(const Pty& pty, const std::filesystem::path& cwd) {
    const pid_t pid = ::fork();
    REQUIRE(pid >= 0);
    if (pid == 0) {
        if (::setsid() < 0) {
            _exit(126);
        }
        const int slave = ::open(pty.slave_path().c_str(), O_RDWR);
        if (slave < 0) {
            _exit(126);
        }
        ::dup2(slave, STDIN_FILENO);
        ::dup2(slave, STDOUT_FILENO);
        ::dup2(slave, STDERR_FILENO);
        if (slave > STDERR_FILENO) {
            ::close(slave);
        }
        if (::chdir(cwd.c_str()) != 0) {
            _exit(126);
        }
        char arg0[] = "cup";
        char arg1[] = "new";
        char arg2[] = "demo";
        char* const argv[] = {arg0, arg1, arg2, nullptr};
        ::execv(CUP_TEST_BIN, argv);
        _exit(127);
    }
    return pid;
}

void assert_graceful_abort(pid_t pid, const Pty& pty, std::string_view seen_before) {
    int status = 0;
    if (!wait_for_exit(pid, status, std::chrono::seconds(5))) {
        ::kill(pid, SIGKILL);
        int reaped = 0;
        ::waitpid(pid, &reaped, 0);
        FAIL("process did not exit within the timeout");
        return;
    }
    const std::string tail = read_until(pty.master(), "aborted.", std::chrono::milliseconds(500));
    INFO("child output: " << seen_before << tail);
    REQUIRE_FALSE(WIFSIGNALED(status));
    REQUIRE(WIFEXITED(status));
    REQUIRE(WEXITSTATUS(status) == 1);
    REQUIRE((std::string(seen_before) + tail).find("aborted.") != std::string::npos);
}

}

TEST_CASE("a real Ctrl-C aborts an interactive prompt instead of killing the process",
          "[ui][signal][pty]") {
    const TempDir dir;
    Pty pty;
    const pid_t pid = spawn_cup_new(pty, dir.path());

    const std::string seen = read_until(pty.master(), "build system?", std::chrono::seconds(5));
    INFO("child output before Ctrl-C: " << seen);
    REQUIRE(seen.find("build system?") != std::string::npos);

    REQUIRE(::write(pty.master(), "\x03", 1) == 1);

    assert_graceful_abort(pid, pty, seen);
}

// Control: Ctrl-D already worked before this fix (it aborts via a plain
// EOF-shaped Event, not a signal), so this should pass unmodified -- it
// exists to guard against a future regression on the same real-pty path.
TEST_CASE("a real Ctrl-D aborts an interactive prompt the same way (control)",
          "[ui][signal][pty]") {
    const TempDir dir;
    Pty pty;
    const pid_t pid = spawn_cup_new(pty, dir.path());

    const std::string seen = read_until(pty.master(), "build system?", std::chrono::seconds(5));
    INFO("child output before Ctrl-D: " << seen);
    REQUIRE(seen.find("build system?") != std::string::npos);

    REQUIRE(::write(pty.master(), "\x04", 1) == 1);

    assert_graceful_abort(pid, pty, seen);
}
