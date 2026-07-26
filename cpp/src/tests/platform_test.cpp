// No Go counterpart: the terminal seam is new code in the port.
//
// Go got raw mode from golang.org/x/term — MakeRaw plus a deferred Restore — so
// there was nothing of cup's own to test. RawMode replaces that defer with a
// destructor, and it is the one object in cup whose failure outlives the process: a
// guard that forgets to restore leaves the user's shell with no echo and no line
// editing. So ownership is tested directly here rather than through cup.ui, which
// only ever exercises the happy path.

#include <catch2/catch_test_macros.hpp>

#include <fcntl.h>
#include <stdlib.h>  // posix_openpt, grantpt, unlockpt, ptsname
#include <termios.h>
#include <unistd.h>

// enter_raw_mode returns std::expected<RawMode, Error>, and a module re-exports
// nothing from its global module fragment — the same rule the <functional> note in
// ui_test.cpp explains.
#include <expected>
#include <string>
#include <string_view>
#include <utility>

// The HTTP cases below read a file:// URL out of a scratch directory.
#include "TempDir.hpp"

// cup.error is not imported: cup.platform re-exports it, because Error appears in
// enter_raw_mode's return type. Naming cup::error below therefore also checks that
// the re-export is real.
import cup.platform;

namespace {

using cup::error::Error;
using cup::platform::RawMode;

// Pty is a pseudo-terminal pair, and it is what makes this suite possible at all:
// ctest hands a test a pipe for stdin, so there is no terminal lying around to put
// into raw mode. The slave end is a real tty — isatty(2) says so — and it belongs
// to nobody, so its settings can be mangled freely.
class Pty {
public:
    Pty() {
        master_ = ::posix_openpt(O_RDWR | O_NOCTTY);
        REQUIRE(master_ >= 0);
        REQUIRE(::grantpt(master_) == 0);
        REQUIRE(::unlockpt(master_) == 0);
        const char* const name = ::ptsname(master_);
        REQUIRE(name != nullptr);
        // O_NOCTTY again: this must not become the test process's controlling
        // terminal, or a stray signal would land on the runner.
        slave_ = ::open(name, O_RDWR | O_NOCTTY);
        REQUIRE(slave_ >= 0);
    }

    Pty(const Pty&) = delete;
    Pty& operator=(const Pty&) = delete;

    ~Pty() {
        ::close(slave_);
        ::close(master_);
    }

    // fd is the slave end — the descriptor handed to cup.platform.
    [[nodiscard]] int fd() const { return slave_; }

    // settings reads the slave's current termios, failing the test if it cannot.
    [[nodiscard]] termios settings() const {
        termios current{};
        REQUIRE(::tcgetattr(slave_, &current) == 0);
        return current;
    }

private:
    int master_ = -1;
    int slave_ = -1;
};

// Pipe supplies the descriptor the failure paths need: open, readable, and not a
// terminal.
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

// cooked reports whether a terminal is still in its default mode: echoing,
// line-buffered, and translating output newlines. Raw mode clears all three, so
// this one predicate distinguishes "restored" from "still raw" throughout.
[[nodiscard]] bool cooked(const termios& settings) {
    return (settings.c_lflag & static_cast<tcflag_t>(ECHO | ICANON)) != 0 &&
           (settings.c_oflag & static_cast<tcflag_t>(OPOST)) != 0;
}

}  // namespace

// is_tty is what every prompt in cup.ui asks before trying raw mode, and what
// decides between the arrow-key menu and the numbered fallback.
TEST_CASE("is_tty tells a terminal from a pipe", "[platform][tty]") {
    const Pty pty;
    const Pipe pipe;

    REQUIRE(cup::platform::is_tty(pty.fd()));
    REQUIRE_FALSE(cup::platform::is_tty(pipe.fd()));
    REQUIRE_FALSE(cup::platform::is_tty(-1));  // not a descriptor at all
}

// The constants exist so callers need no <unistd.h>; that is only true if they
// carry the values POSIX fixes.
TEST_CASE("the exported descriptors are the standard three", "[platform][fd]") {
    REQUIRE(cup::platform::kStdinFd == 0);
    REQUIRE(cup::platform::kStdoutFd == 1);
    REQUIRE(cup::platform::kStderrFd == 2);
}

TEST_CASE("enter_raw_mode clears the cooked-mode flags and the guard puts them back",
          "[platform][raw]") {
    const Pty pty;
    const termios before = pty.settings();
    REQUIRE(cooked(before));  // a fresh pty starts echoing and line-buffered

    {
        const auto raw = cup::platform::enter_raw_mode(pty.fd());
        REQUIRE(raw.has_value());
        REQUIRE(raw->owns());

        // The flags cfmakeraw clears — the set golang.org/x/term's MakeRaw cleared
        // for the Go implementation, which is why key decoding ports unchanged.
        const termios during = pty.settings();
        REQUIRE((during.c_lflag &
                 static_cast<tcflag_t>(ECHO | ECHONL | ICANON | ISIG | IEXTEN)) == 0);
        REQUIRE((during.c_oflag & static_cast<tcflag_t>(OPOST)) == 0);
        REQUIRE((during.c_iflag & static_cast<tcflag_t>(ICRNL | IXON | ISTRIP)) == 0);
        REQUIRE((during.c_cflag & static_cast<tcflag_t>(CS8)) != 0);
        // One byte returns from read, with no inter-byte timer: what makes a single
        // keypress readable instead of a whole line.
        REQUIRE(during.c_cc[VMIN] == 1);
        REQUIRE(during.c_cc[VTIME] == 0);
    }

    // The destructor ran on the way out of the block — the `defer term.Restore`
    // this class replaces.
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

    // A guard that has already restored must not write the terminal again — by then
    // something else may own it. Turning echo off by hand and calling restore a
    // second time is how that shows: a second tcsetattr would undo the change.
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
    // The move must not have restored on the way through: the terminal stays raw
    // until whoever holds it now says otherwise.
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
    // The overwritten guard's terminal came back; the one it now holds is still raw.
    REQUIRE(given_up.settings().c_lflag == given_up_before.c_lflag);
    REQUIRE_FALSE(cooked(taken_over.settings()));
}

TEST_CASE("self-assignment leaves a guard holding its terminal", "[platform][raw][move]") {
    const Pty pty;

    auto raw = cup::platform::enter_raw_mode(pty.fd());
    REQUIRE(raw.has_value());

    // Assigned through a reference so this reads as the aliasing case the operator
    // guards against, rather than as the self-move a compiler diagnoses on sight.
    RawMode& alias = *raw;
    alias = std::move(*raw);

    REQUIRE(raw->owns());
    REQUIRE_FALSE(cooked(pty.settings()));  // not restored out from under itself
}

TEST_CASE("enter_raw_mode reports failure off a terminal", "[platform][raw]") {
    const Pipe pipe;

    const auto raw = cup::platform::enter_raw_mode(pipe.fd());
    REQUIRE_FALSE(raw.has_value());
    // The message ends in strerror(errno), so only the call that failed is pinned.
    REQUIRE(raw.error().message().starts_with("tcgetattr: "));
    // And it is an ordinary error, not the abort sentinel: cup.ui reads this as
    // "not an interactive terminal" and falls back to the numbered prompt.
    REQUIRE(raw.error().kind() == Error::Kind::General);
    REQUIRE_FALSE(cup::error::is_abort(raw.error()));
}

// --- the HTTP seam ----------------------------------------------------------
//
// No Go counterpart either: Go gets net/http from the standard library, so its
// suite tests scaffold's *use* of it (httpGet against an httptest server) rather
// than a transport of its own. curl_get is cup's code, and these are the parts of
// it a caller depends on.
//
// The transport is exercised over file://, which libcurl speaks natively: it is a
// real curl_easy_perform, on the real callbacks, with no network and no server to
// stand up. It is also why curl_get treats a zero response code as success — see
// the note there.

TEST_CASE("http_get reads a URL through libcurl", "[platform][http]") {
    const cup::test::TempDir dir;
    dir.write("body.json", R"({"results":[{"name":"14"}]})");

    const auto body = cup::platform::http_get("file://" + (dir.path() / "body.json").string());
    REQUIRE(body.has_value());
    REQUIRE(*body == R"({"results":[{"name":"14"}]})");
}

TEST_CASE("http_get reports a transport failure", "[platform][http]") {
    const cup::test::TempDir dir;

    // A URL that resolves to nothing: libcurl fails before any body arrives.
    const auto missing = cup::platform::http_get("file://" + (dir.path() / "absent").string());
    REQUIRE_FALSE(missing.has_value());
    REQUIRE(missing.error().message().starts_with("GET file://"));

    // And a URL that is not one at all.
    const auto malformed = cup::platform::http_get("://not-a-url");
    REQUIRE_FALSE(malformed.has_value());
    REQUIRE(malformed.error().kind() == Error::Kind::General);
}

TEST_CASE("ScopedHttpGet substitutes the fetcher and puts it back", "[platform][http]") {
    const cup::test::TempDir dir;
    dir.write("body.txt", "real");

    const std::string url = "file://" + (dir.path() / "body.txt").string();
    {
        const cup::platform::ScopedHttpGet scoped(
            [](std::string_view) -> std::expected<std::string, Error> { return "stubbed"; });
        REQUIRE(cup::platform::http_get(url) == "stubbed");
    }
    // Restored: the real transport is back, and it reads the file.
    REQUIRE(cup::platform::http_get(url) == "real");
}
