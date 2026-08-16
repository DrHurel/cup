
#include <catch2/catch_test_macros.hpp>

#include <fcntl.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "TempDir.hpp"
#include "TestHttpServer.hpp"

import cup.platform;

namespace {

using cup::error::Error;
using cup::platform::RawMode;
using cup::test::TempDir;

// Restores a mutable global (an overridable hook) to its prior value when the
// test ends, mirroring releases_test.cpp's ScopedOverride.
template <typename T>
class ScopedOverride {
public:
    ScopedOverride(T& slot, T value) : slot_(slot), previous_(slot) { slot_ = std::move(value); }
    ~ScopedOverride() { slot_ = std::move(previous_); }
    ScopedOverride(const ScopedOverride&) = delete;
    ScopedOverride& operator=(const ScopedOverride&) = delete;

private:
    T& slot_;
    T previous_;
};

class Pty {
public:
    Pty() {
        master_ = ::posix_openpt(O_RDWR | O_NOCTTY);
        REQUIRE(master_ >= 0);
        REQUIRE(::grantpt(master_) == 0);
        REQUIRE(::unlockpt(master_) == 0);
        const char* const name = ::ptsname(master_);
        REQUIRE(name != nullptr);
        slave_ = ::open(name, O_RDWR | O_NOCTTY);
        REQUIRE(slave_ >= 0);
    }

    Pty(const Pty&) = delete;
    Pty& operator=(const Pty&) = delete;

    ~Pty() {
        ::close(slave_);
        ::close(master_);
    }

    [[nodiscard]] int fd() const { return slave_; }

    [[nodiscard]] termios settings() const {
        termios current{};
        REQUIRE(::tcgetattr(slave_, &current) == 0);
        return current;
    }

private:
    int master_ = -1;
    int slave_ = -1;
};

class Pipe {
public:
    Pipe() {
        int fds[2]{};
        REQUIRE(::pipe(fds) == 0);
        read_ = fds[0];
        write_ = fds[1];
    }

    Pipe(const Pipe&) = delete;
    Pipe& operator=(const Pipe&) = delete;

    ~Pipe() {
        ::close(read_);
        ::close(write_);
    }

    [[nodiscard]] int fd() const { return read_; }

private:
    int read_ = -1;
    int write_ = -1;
};

[[nodiscard]] bool cooked(const termios& settings) {
    return (settings.c_lflag & static_cast<tcflag_t>(ECHO | ICANON)) != 0 &&
           (settings.c_oflag & static_cast<tcflag_t>(OPOST)) != 0;
}

}

TEST_CASE("is_tty tells a terminal from a pipe", "[platform][tty]") {
    const Pty pty;
    const Pipe pipe;

    REQUIRE(cup::platform::is_tty(pty.fd()));
    REQUIRE_FALSE(cup::platform::is_tty(pipe.fd()));
    REQUIRE_FALSE(cup::platform::is_tty(-1));
}

TEST_CASE("the exported descriptors are the standard three", "[platform][fd]") {
    REQUIRE(cup::platform::kStdinFd == 0);
    REQUIRE(cup::platform::kStdoutFd == 1);
    REQUIRE(cup::platform::kStderrFd == 2);
}

TEST_CASE("enter_raw_mode clears the cooked-mode flags and the guard puts them back",
          "[platform][raw]") {
    const Pty pty;
    const termios before = pty.settings();
    REQUIRE(cooked(before));

    {
        const auto raw = cup::platform::enter_raw_mode(pty.fd());
        REQUIRE(raw.has_value());
        REQUIRE(raw->owns());

        const termios during = pty.settings();
        REQUIRE((during.c_lflag &
                 static_cast<tcflag_t>(ECHO | ECHONL | ICANON | ISIG | IEXTEN)) == 0);
        REQUIRE((during.c_oflag & static_cast<tcflag_t>(OPOST)) == 0);
        REQUIRE((during.c_iflag & static_cast<tcflag_t>(ICRNL | IXON | ISTRIP)) == 0);
        REQUIRE((during.c_cflag & static_cast<tcflag_t>(CS8)) != 0);
        REQUIRE(during.c_cc[VMIN] == 1);
        REQUIRE(during.c_cc[VTIME] == 0);
    }

    const termios after = pty.settings();
    REQUIRE(cooked(after));
    REQUIRE(after.c_iflag == before.c_iflag);
    REQUIRE(after.c_oflag == before.c_oflag);
    REQUIRE(after.c_lflag == before.c_lflag);
    REQUIRE(after.c_cflag == before.c_cflag);
    REQUIRE(after.c_cc[VMIN] == before.c_cc[VMIN]);
    REQUIRE(after.c_cc[VTIME] == before.c_cc[VTIME]);
}

TEST_CASE("restore hands the terminal back early, and never twice", "[platform][raw]") {
    const Pty pty;
    const termios before = pty.settings();

    auto raw = cup::platform::enter_raw_mode(pty.fd());
    REQUIRE(raw.has_value());

    raw->restore();
    REQUIRE_FALSE(raw->owns());
    REQUIRE(cooked(pty.settings()));
    REQUIRE(pty.settings().c_lflag == before.c_lflag);

    termios meddled = before;
    meddled.c_lflag &= ~static_cast<tcflag_t>(ECHO);
    REQUIRE(::tcsetattr(pty.fd(), TCSAFLUSH, &meddled) == 0);

    raw->restore();
    REQUIRE((pty.settings().c_lflag & static_cast<tcflag_t>(ECHO)) == 0);
}

TEST_CASE("a moved-from guard owns nothing and restores nothing", "[platform][raw][move]") {
    const Pty pty;
    const termios before = pty.settings();

    auto raw = cup::platform::enter_raw_mode(pty.fd());
    REQUIRE(raw.has_value());

    RawMode moved(std::move(*raw));
    REQUIRE(moved.owns());
    REQUIRE_FALSE(raw->owns());
    REQUIRE_FALSE(cooked(pty.settings()));

    moved.restore();
    REQUIRE(pty.settings().c_lflag == before.c_lflag);
}

TEST_CASE("move assignment restores the terminal it gives up", "[platform][raw][move]") {
    const Pty given_up;
    const Pty taken_over;
    const termios given_up_before = given_up.settings();

    auto held = cup::platform::enter_raw_mode(given_up.fd());
    auto other = cup::platform::enter_raw_mode(taken_over.fd());
    REQUIRE(held.has_value());
    REQUIRE(other.has_value());

    *held = std::move(*other);

    REQUIRE(held->owns());
    REQUIRE_FALSE(other->owns());
    REQUIRE(given_up.settings().c_lflag == given_up_before.c_lflag);
    REQUIRE_FALSE(cooked(taken_over.settings()));
}

TEST_CASE("self-assignment leaves a guard holding its terminal", "[platform][raw][move]") {
    const Pty pty;

    auto raw = cup::platform::enter_raw_mode(pty.fd());
    REQUIRE(raw.has_value());

    RawMode& alias = *raw;
    alias = std::move(*raw);

    REQUIRE(raw->owns());
    REQUIRE_FALSE(cooked(pty.settings()));
}

TEST_CASE("enter_raw_mode reports failure off a terminal", "[platform][raw]") {
    const Pipe pipe;

    const auto raw = cup::platform::enter_raw_mode(pipe.fd());
    REQUIRE_FALSE(raw.has_value());
    REQUIRE(raw.error().message().starts_with("tcgetattr: "));
    REQUIRE(raw.error().kind() == Error::Kind::General);
    REQUIRE_FALSE(cup::error::is_abort(raw.error()));
}

TEST_CASE("http_get returns a 200 response's body", "[platform][http]") {
    using cup::test::TestHttpServer;
    const TestHttpServer server(
        [](const std::string&) { return TestHttpServer::Response{200, "hello"}; });

    const auto body = cup::platform::http_get(server.url() + "/ok");
    REQUIRE(body.has_value());
    REQUIRE(*body == "hello");
}

TEST_CASE("http_get reports a non-200 response as an error", "[platform][http]") {
    using cup::test::TestHttpServer;
    const TestHttpServer server(
        [](const std::string&) { return TestHttpServer::Response{404, "not found"}; });

    const auto body = cup::platform::http_get(server.url() + "/missing");
    REQUIRE_FALSE(body.has_value());
}

TEST_CASE("http_get reports a malformed url as an error", "[platform][http]") {
    const auto body = cup::platform::http_get("://not-a-url");
    REQUIRE_FALSE(body.has_value());
}

TEST_CASE("run_command succeeds for a zero-exit program", "[platform][process]") {
    const TempDir dir;
    const auto result = cup::platform::run_command(dir.path(), "true", {});
    REQUIRE(result.has_value());
}

TEST_CASE("run_command reports a non-zero exit as an error", "[platform][process]") {
    const TempDir dir;
    const auto result = cup::platform::run_command(dir.path(), "false", {});
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().message() == "command failed: false : exit status 1");
    REQUIRE(result.error().kind() == Error::Kind::General);
}

TEST_CASE("run_command reports a missing binary as an error", "[platform][process]") {
    const TempDir dir;
    const auto result = cup::platform::run_command(dir.path(), "cup-test-does-not-exist", {});
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().message().starts_with("command failed: cup-test-does-not-exist"));
}

TEST_CASE("run_command runs the program with dir as its working directory",
          "[platform][process]") {
    const TempDir dir;
    const std::vector<std::string> args{"-c", "touch marker"};
    const auto result = cup::platform::run_command(dir.path(), "sh", args);
    REQUIRE(result.has_value());
    REQUIRE(std::filesystem::exists(dir.path() / "marker"));
}

TEST_CASE("run_command passes args through to the child", "[platform][process]") {
    const TempDir dir;
    const std::vector<std::string> args{"-c", "exit $1", "_", "7"};
    const auto result = cup::platform::run_command(dir.path(), "sh", args);
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().message().ends_with("exit status 7"));
}

TEST_CASE("run_command reports a signal-terminated child as an error", "[platform][process]") {
    const TempDir dir;
    const std::vector<std::string> args{"-c", "kill -TERM $$"};
    const auto result = cup::platform::run_command(dir.path(), "sh", args);
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().message().ends_with("signal 15"));
}

TEST_CASE("run_command_func is the override point for run_command", "[platform][process]") {
    ScopedOverride override_func(
        cup::platform::run_command_func(),
        cup::platform::RunCommandFunc{[](const std::filesystem::path&, std::string_view,
                                         std::span<const std::string>)
                                          -> std::expected<void, Error> {
            return std::unexpected(Error("stubbed"));
        }});
    const TempDir dir;
    const auto result = cup::platform::run_command(dir.path(), "true", {});
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().message() == "stubbed");
}

TEST_CASE("capture_command returns a zero-exit program's stdout", "[platform][process]") {
    const TempDir dir;
    const std::vector<std::string> args{"-c", "printf hello"};
    const auto result = cup::platform::capture_command(dir.path(), "sh", args);
    REQUIRE(result.has_value());
    REQUIRE(*result == "hello");
}

TEST_CASE("capture_command reports a non-zero exit as an error", "[platform][process]") {
    const TempDir dir;
    const auto result = cup::platform::capture_command(dir.path(), "false", {});
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().message() == "command failed: false : exit status 1");
}

TEST_CASE("capture_command reports a missing binary as an error", "[platform][process]") {
    const TempDir dir;
    const auto result = cup::platform::capture_command(dir.path(), "cup-test-does-not-exist", {});
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().message().starts_with("command failed: cup-test-does-not-exist"));
}

TEST_CASE("capture_command runs the program with dir as its working directory",
          "[platform][process]") {
    const TempDir dir;
    const std::vector<std::string> args{"-c", "pwd"};
    const auto result = cup::platform::capture_command(dir.path(), "sh", args);
    REQUIRE(result.has_value());
    REQUIRE(*result == std::filesystem::canonical(dir.path()).string() + "\n");
}

TEST_CASE("capture_command reports a signal-terminated child as an error", "[platform][process]") {
    const TempDir dir;
    const std::vector<std::string> args{"-c", "kill -TERM $$"};
    const auto result = cup::platform::capture_command(dir.path(), "sh", args);
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().message().ends_with("signal 15"));
}

TEST_CASE("capture_command passes args through to the child", "[platform][process]") {
    const TempDir dir;
    const std::vector<std::string> args{"-c", "exit $1", "_", "7"};
    const auto result = cup::platform::capture_command(dir.path(), "sh", args);
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().message().ends_with("exit status 7"));
}

TEST_CASE("capture_command_func is the override point for capture_command", "[platform][process]") {
    ScopedOverride override_func(
        cup::platform::capture_command_func(),
        cup::platform::CaptureCommandFunc{[](const std::filesystem::path&, std::string_view,
                                             std::span<const std::string>)
                                              -> std::expected<std::string, Error> {
            return std::unexpected(Error("stubbed"));
        }});
    const TempDir dir;
    const auto result = cup::platform::capture_command(dir.path(), "true", {});
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().message() == "stubbed");
}
