
#include <catch2/catch_test_macros.hpp>

#include <fcntl.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

#include <expected>
#include <string>
#include <utility>

import cup.platform;

namespace {

using cup::error::Error;
using cup::platform::RawMode;

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
