// Port of internal/ui/ui_test.go and internal/ui/select_test.go.
//
// The Go tests already pass against the Go implementation, so they define the
// behaviour precisely; each TEST_CASE below names the Go test it replaces.

#include <catch2/catch_test_macros.hpp>

#include <fcntl.h>
#include <stdlib.h>  // posix_openpt, grantpt, unlockpt, ptsname
#include <termios.h>
#include <unistd.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <expected>
// cup.ui exports Validator as a std::function alias, but a module only re-exports
// declarations from its global module fragment — instantiating the template here
// needs the definition, so this consumer includes <functional> itself.
#include <functional>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

import cup.ui;
import cup.error;
import cup.probe;
// select_one branches on whether stdin is a terminal, and the fallback case below
// asserts that condition rather than assuming it. cup.ui imports cup.platform
// without re-exporting it — the terminal stays behind the seam — so the test names
// it itself.
import cup.platform;

namespace {

using cup::error::Error;

// ScopedColor forces colouring on or off for one test and restores it after, so
// the suite does not depend on whether stdout happens to be a terminal.
class ScopedColor {
public:
    explicit ScopedColor(bool enabled) : previous_(cup::ui::set_use_color(enabled)) {}
    ~ScopedColor() { cup::ui::set_use_color(previous_); }

private:
    bool previous_;
};

// ScopedStdin replaces descriptor 0 with a pipe holding keystrokes, and puts the
// original back afterwards.
//
// The arrow-key menu needs this rather than ScopedInput because read_key bypasses
// ScopedInput deliberately: raw-mode key handling only ever applies to a real
// terminal, so there is no stream to substitute — only a descriptor.
class ScopedStdin {
public:
    explicit ScopedStdin(std::string_view keys) {
        int fds[2]{};
        REQUIRE(::pipe(fds) == 0);
        saved_ = ::dup(STDIN_FILENO);
        REQUIRE(saved_ >= 0);
        REQUIRE(::write(fds[1], keys.data(), keys.size()) == static_cast<ssize_t>(keys.size()));
        // Closed at once, so the menu reaches end-of-input after the last key
        // instead of blocking there. The abort cases rely on it too.
        REQUIRE(::close(fds[1]) == 0);
        REQUIRE(::dup2(fds[0], STDIN_FILENO) == STDIN_FILENO);
        REQUIRE(::close(fds[0]) == 0);
    }

    ScopedStdin(const ScopedStdin&) = delete;
    ScopedStdin& operator=(const ScopedStdin&) = delete;

    ~ScopedStdin() {
        ::dup2(saved_, STDIN_FILENO);
        ::close(saved_);
    }

private:
    int saved_ = -1;
};

// ScopedTerminalStdin puts a real pseudo-terminal on descriptor 0, types keys into
// it, and puts the original descriptor back afterwards.
//
// ScopedStdin cannot stand in for it. select_one asks platform::is_tty before doing
// anything else and takes the numbered fallback when the answer is no — so with a
// pipe on descriptor 0 the whole interactive path, raw mode included, is
// unreachable. Only a terminal gets there, and ctest hands the suite a pipe.
//
// The keys are typed from a second thread, which is not incidental: enter_raw_mode
// applies its settings with TCSAFLUSH, and that *discards* whatever input is already
// queued. Keys written before the menu starts are therefore thrown away by the very
// call the test is here to exercise, leaving it blocked on a keypress that has
// already been eaten — the flake this shape exists to avoid. So the thread waits for
// the terminal to actually leave canonical mode, and types only then.
class ScopedTerminalStdin {
public:
    explicit ScopedTerminalStdin(std::string_view keys) : keys_(keys) {
        master_ = ::posix_openpt(O_RDWR | O_NOCTTY);
        REQUIRE(master_ >= 0);
        REQUIRE(::grantpt(master_) == 0);
        REQUIRE(::unlockpt(master_) == 0);
        const char* const name = ::ptsname(master_);
        REQUIRE(name != nullptr);
        // O_NOCTTY: this must not become the runner's controlling terminal, or a
        // keystroke the line discipline turns into a signal would land on it.
        slave_ = ::open(name, O_RDWR | O_NOCTTY);
        REQUIRE(slave_ >= 0);

        saved_ = ::dup(STDIN_FILENO);
        REQUIRE(saved_ >= 0);
        REQUIRE(::dup2(slave_, STDIN_FILENO) == STDIN_FILENO);

        typist_ = std::thread([this] { type(); });
    }

    ScopedTerminalStdin(const ScopedTerminalStdin&) = delete;
    ScopedTerminalStdin& operator=(const ScopedTerminalStdin&) = delete;

    ~ScopedTerminalStdin() {
        typist_.join();
        ::dup2(saved_, STDIN_FILENO);
        ::close(saved_);
        ::close(slave_);
        if (master_ >= 0) {
            ::close(master_);
        }
    }

    // typed_in_raw_mode reports whether the keys went in after the menu took the
    // terminal, which is the condition the wait below can time out on. Asserted by
    // the test so a menu that never entered raw mode reads as that, rather than as
    // whatever the keys happened to do on the way through the line discipline.
    [[nodiscard]] bool typed_in_raw_mode() const { return raw_seen_; }

private:
    void type() {
        // Canonical mode off is raw mode on: it is the flag select_one clears that
        // the reader depends on. The wait is bounded so a menu that never gets there
        // fails the assertion instead of hanging the suite, and the keys are typed
        // either way so the read cannot block for ever.
        for (int waited = 0; waited < 2000 && !raw_seen_; ++waited) {
            termios now{};
            if (::tcgetattr(slave_, &now) == 0 &&
                (now.c_lflag & static_cast<tcflag_t>(ICANON)) == 0) {
                raw_seen_ = true;
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }

        if (::write(master_, keys_.data(), keys_.size()) !=
            static_cast<ssize_t>(keys_.size())) {
            // Catch2's assertions are not thread-safe, so a failed write cannot be
            // reported from here. Closing the master ends the menu's input instead:
            // it reads end-of-input and aborts, so the test fails on its result
            // rather than waiting on a key that is never coming. The destructor
            // joins before it closes anything, so this needs no further guard.
            ::close(master_);
            master_ = -1;
        }
    }

    std::string keys_;
    std::atomic<bool> raw_seen_ = false;
    std::thread typist_;
    int master_ = -1;
    int slave_ = -1;
    int saved_ = -1;
};

// CapturedStdout collects what the menu paints, and is not only about assertions: the
// menu backs the cursor up and erases lines, which would chew through the test
// runner's own output on an interactive run.
//
// It has to redirect the descriptor because cup.ui writes through std::print to
// stdout rather than to a stream a test could swap out — that funnelling is forced by
// a GCC 14 module bug, see the note at the top of io.cppm.
class CapturedStdout {
public:
    CapturedStdout() {
        file_ = std::tmpfile();
        REQUIRE(file_ != nullptr);
        saved_ = ::dup(STDOUT_FILENO);
        REQUIRE(saved_ >= 0);
        cup::ui::flush_output();
        REQUIRE(::dup2(::fileno(file_), STDOUT_FILENO) == STDOUT_FILENO);
    }

    CapturedStdout(const CapturedStdout&) = delete;
    CapturedStdout& operator=(const CapturedStdout&) = delete;

    ~CapturedStdout() {
        stop();
        std::fclose(file_);
    }

    // stop ends the capture without discarding anything, so assertions made after it
    // report to the real stdout. Calling it before the first REQUIRE is what keeps a
    // failure message from vanishing into the capture file.
    void stop() {
        if (saved_ < 0) {
            return;
        }
        cup::ui::flush_output();
        ::dup2(saved_, STDOUT_FILENO);
        ::close(saved_);
        saved_ = -1;
    }

    // str ends the capture and returns everything written since construction.
    [[nodiscard]] std::string str() {
        stop();
        std::rewind(file_);
        std::string out;
        std::array<char, 512> chunk{};
        while (const std::size_t n = std::fread(chunk.data(), 1, chunk.size(), file_)) {
            out.append(chunk.data(), n);
        }
        return out;
    }

private:
    std::FILE* file_ = nullptr;
    int saved_ = -1;
};

// kFrameBytes is read_key's buffer size, and so the unit this suite writes keys in.
constexpr std::size_t kFrameBytes = 3;

// frame pads one keypress out to kFrameBytes.
//
// A pipe hands back everything buffered in a single read, and the menu treats one
// read as one keypress — so writing "jk\r" would deliver all three at once and drop
// two of them. Padding each press to exactly three bytes makes every read see
// exactly one, and it decodes identically to that key arriving alone on a terminal:
// the decoder looks at buf[0], and at buf[2] only for the three-byte arrow sequences.
[[nodiscard]] std::string frame(std::string_view press) {
    std::string padded(press);
    padded.resize(kFrameBytes, '\0');
    return padded;
}

// The keys the menu understands, as a terminal sends them.
constexpr std::string_view kUp = "\x{1b}[A";
constexpr std::string_view kDown = "\x{1b}[B";
constexpr std::string_view kVimUp = "k";
constexpr std::string_view kVimDown = "j";
constexpr std::string_view kEnter = "\r";
constexpr std::string_view kLineFeed = "\n";
constexpr std::string_view kCtrlC = "\x{03}";
constexpr std::string_view kCtrlD = "\x{04}";

}  // namespace

// Go: TestColor
TEST_CASE("color honours the colour setting", "[ui][color]") {
    SECTION("disabled leaves the text untouched") {
        const ScopedColor guard(false);
        REQUIRE(cup::ui::color(cup::ui::kCyan, "hi") == "hi");
    }
    SECTION("enabled wraps the text in escapes") {
        const ScopedColor guard(true);
        REQUIRE(cup::ui::color("1", "hi") == "\x{1b}[1mhi\x{1b}[0m");
    }
}

// Go: TestTextUsesDefaultOnEmpty
TEST_CASE("text falls back to the default on empty input", "[ui][text]") {
    std::istringstream in("\n");
    const cup::ui::ScopedInput scoped(in);

    const auto got = cup::ui::text("name?", "fallback");
    REQUIRE(got.has_value());
    REQUIRE(*got == "fallback");
}

// Go: TestTextTrimsAndReturnsInput
TEST_CASE("text trims surrounding whitespace", "[ui][text]") {
    std::istringstream in("  hello  \n");
    const cup::ui::ScopedInput scoped(in);

    const auto got = cup::ui::text("name?", "def");
    REQUIRE(got.has_value());
    REQUIRE(*got == "hello");
}

// Go: TestTextRepeatsUntilValid
TEST_CASE("text repeats until the value validates", "[ui][text]") {
    std::istringstream in("bad\ngood\n");
    const cup::ui::ScopedInput scoped(in);

    const cup::ui::Validator only_good =
        [](std::string_view s) -> std::expected<void, Error> {
        if (s != "good") {
            return std::unexpected(Error("must be good"));
        }
        return {};
    };

    const auto got = cup::ui::text("name?", "", only_good);
    REQUIRE(got.has_value());
    REQUIRE(*got == "good");
}

// Go: TestTextAbortsOnEOF
TEST_CASE("text aborts on EOF", "[ui][text]") {
    std::istringstream in("");  // immediate EOF, no data
    const cup::ui::ScopedInput scoped(in);

    const auto got = cup::ui::text("name?", "");
    REQUIRE_FALSE(got.has_value());
    REQUIRE(cup::error::is_abort(got.error()));
}

// Go: TestConfirm
TEST_CASE("confirm parses answers and honours its default", "[ui][confirm]") {
    struct Case {
        const char* input;
        bool def;
        bool want;
        const char* why;
    };
    const std::array<Case, 8> cases{{
        {"y\n", false, true, "y"},
        {"yes\n", false, true, "yes"},
        {"n\n", true, false, "n"},
        {"no\n", true, false, "no"},
        {"Y\n", false, true, "case-insensitive"},
        {"\n", true, true, "empty takes the true default"},
        {"\n", false, false, "empty takes the false default"},
        {"maybe\ny\n", false, true, "unrecognised answer repeats"},
    }};

    for (const auto& c : cases) {
        INFO(c.why);
        std::istringstream in(c.input);
        const cup::ui::ScopedInput scoped(in);

        const auto got = cup::ui::confirm("ok?", c.def);
        REQUIRE(got.has_value());
        REQUIRE(*got == c.want);
    }
}

// Go: TestConfirmAbortsOnEOF
TEST_CASE("confirm aborts on EOF", "[ui][confirm]") {
    std::istringstream in("");
    const cup::ui::ScopedInput scoped(in);

    const auto got = cup::ui::confirm("ok?", false);
    REQUIRE_FALSE(got.has_value());
    REQUIRE(cup::error::is_abort(got.error()));
}

// Go: TestStatusLinesDoNotPanic
TEST_CASE("the status helpers emit without throwing", "[ui][status]") {
    REQUIRE_NOTHROW(cup::ui::running("running message"));
    REQUIRE_NOTHROW(cup::ui::wrote("wrote message"));
    REQUIRE_NOTHROW(cup::ui::updated("updated message"));
    REQUIRE_NOTHROW(cup::ui::skipped("skipped message"));
    REQUIRE_NOTHROW(cup::ui::removed("removed message"));
    REQUIRE_NOTHROW(cup::ui::next("next message"));
    REQUIRE_NOTHROW(cup::ui::accent("accent message"));
    REQUIRE_NOTHROW(cup::ui::success("success message"));
    REQUIRE_NOTHROW(cup::ui::err("err message"));
}

// Go: TestIndexOf
TEST_CASE("index_of finds an option, or falls back to the first", "[ui][select]") {
    const std::vector<std::string> options{"a", "b", "c"};
    REQUIRE(cup::ui::detail::index_of(options, "a") == 0);
    REQUIRE(cup::ui::detail::index_of(options, "b") == 1);
    REQUIRE(cup::ui::detail::index_of(options, "c") == 2);
    REQUIRE(cup::ui::detail::index_of(options, "z") == 0);  // absent -> 0
    REQUIRE(cup::ui::detail::index_of(options, "") == 0);
}

// Go: TestIsUpKey
TEST_CASE("is_up_key decodes k and the up-arrow sequence", "[ui][select][keys]") {
    const std::array<unsigned char, 3> vim{'k', 0, 0};
    const std::array<unsigned char, 3> arrow{27, '[', 'A'};
    REQUIRE(cup::ui::detail::is_up_key(vim, 1));
    REQUIRE(cup::ui::detail::is_up_key(arrow, 3));

    const std::array<unsigned char, 3> down_vim{'j', 0, 0};
    const std::array<unsigned char, 3> down_arrow{27, '[', 'B'};
    const std::array<unsigned char, 3> other{'x', 0, 0};
    REQUIRE_FALSE(cup::ui::detail::is_up_key(down_vim, 1));
    REQUIRE_FALSE(cup::ui::detail::is_up_key(down_arrow, 3));
    REQUIRE_FALSE(cup::ui::detail::is_up_key(other, 1));
    // Right bytes, wrong length: a lone ESC is not an arrow press.
    REQUIRE_FALSE(cup::ui::detail::is_up_key(arrow, 1));
    // Nothing read at all: no bytes to index, so no key.
    REQUIRE_FALSE(cup::ui::detail::is_up_key({}, 0));
}

// Go: TestIsDownKey
TEST_CASE("is_down_key decodes j and the down-arrow sequence", "[ui][select][keys]") {
    const std::array<unsigned char, 3> vim{'j', 0, 0};
    const std::array<unsigned char, 3> arrow{27, '[', 'B'};
    REQUIRE(cup::ui::detail::is_down_key(vim, 1));
    REQUIRE(cup::ui::detail::is_down_key(arrow, 3));

    const std::array<unsigned char, 3> up_vim{'k', 0, 0};
    const std::array<unsigned char, 3> up_arrow{27, '[', 'A'};
    REQUIRE_FALSE(cup::ui::detail::is_down_key(up_vim, 1));
    REQUIRE_FALSE(cup::ui::detail::is_down_key(up_arrow, 3));
    REQUIRE_FALSE(cup::ui::detail::is_down_key(arrow, 1));  // wrong length
    REQUIRE_FALSE(cup::ui::detail::is_down_key({}, 0));     // nothing read
}

// No Go counterpart: Go's selectInteractive read the terminal directly and the Go
// suite had no way to script it, so only the key decoders and the numbered fallback
// were ever tested. The port can do better — read_key takes its bytes from
// descriptor 0, and a pipe there drives the whole menu.
TEST_CASE("select_interactive walks the list and returns the highlighted option",
          "[ui][select][keys]") {
    const std::vector<std::string> options{"red", "green", "blue"};

    const struct Case {
        std::string keys;
        std::size_t start;
        const char* want;
        const char* why;
    } cases[]{
        {frame(kEnter), 0, "red", "enter takes the option under the cursor"},
        {frame(kDown) + frame(kEnter), 0, "green", "down moves one"},
        {frame(kDown) + frame(kDown) + frame(kEnter), 0, "blue", "and again"},
        {frame(kUp) + frame(kEnter), 0, "blue", "up from the first wraps to the last"},
        {frame(kDown) + frame(kEnter), 2, "red", "down from the last wraps to the first"},
        {frame(kVimDown) + frame(kEnter), 0, "green", "j moves down"},
        {frame(kVimUp) + frame(kEnter), 1, "red", "k moves up"},
        {frame("x") + frame(kEnter), 1, "green", "a key with no meaning is ignored"},
        {frame(kLineFeed), 1, "green", "a terminal sending LF rather than CR commits too"},
        {frame(kDown) + frame(kUp) + frame(kEnter), 1, "green", "down then up is where it began"},
    };

    for (const auto& c : cases) {
        INFO(c.why);
        const ScopedStdin keys(c.keys);
        CapturedStdout painted;

        const auto got = cup::ui::detail::select_interactive("pick?", options, c.start);
        painted.stop();

        REQUIRE(got.has_value());
        REQUIRE(*got == c.want);
    }
}

// The abort sentinel matters as much as the choice: `cup new` turns it into a clean
// "aborted." exit rather than an error report.
TEST_CASE("select_interactive aborts on Ctrl+C, Ctrl+D and a closed input",
          "[ui][select][keys]") {
    const std::vector<std::string> options{"red", "green"};

    const struct Case {
        std::string keys;
        const char* why;
    } cases[]{
        {frame(kCtrlC), "Ctrl+C"},
        {frame(kCtrlD), "Ctrl+D"},
        {std::string(), "the input ended with no key pressed"},
    };

    for (const auto& c : cases) {
        INFO(c.why);
        const ScopedStdin keys(c.keys);
        CapturedStdout painted;

        const auto got = cup::ui::detail::select_interactive("pick?", options, 0);
        painted.stop();

        REQUIRE_FALSE(got.has_value());
        REQUIRE(cup::error::is_abort(got.error()));
    }
}

// No Go counterpart, and the bytes are the point: the menu redraws itself in place,
// so a wrong cursor-up count smears it across the scrollback instead of replacing it.
TEST_CASE("select_interactive redraws in place and marks the pick", "[ui][select][render]") {
    const ScopedColor colour(true);
    const std::vector<std::string> options{"red", "green", "blue"};
    const ScopedStdin keys(frame(kDown) + frame(kEnter));

    CapturedStdout capture;
    const auto got = cup::ui::detail::select_interactive("pick?", options, 0);
    const std::string painted = capture.str();  // stdout is the terminal's again here

    REQUIRE(got.has_value());
    REQUIRE(*got == "green");
    REQUIRE(painted ==
            // The question, then the list with the cursor on the starting option.
            "\x{1b}[38;5;81;1m?\x{1b}[0m pick? \x{1b}[38;5;244m(up/down, enter)\x{1b}[0m\r\n"
            "\x{1b}[38;5;81;1m> \x{1b}[0m\x{1b}[38;5;81;1mred\x{1b}[0m\r\n"
            "  green\r\n"
            "  blue\r\n"
            // Down: back up over the three printed lines, clearing each, and repaint.
            "\x{1b}[1A\x{1b}[2K\x{1b}[1A\x{1b}[2K\x{1b}[1A\x{1b}[2K"
            "  red\r\n"
            "\x{1b}[38;5;81;1m> \x{1b}[0m\x{1b}[38;5;81;1mgreen\x{1b}[0m\r\n"
            "  blue\r\n"
            // Enter: the same walk back, then the final list — the pick in green, the
            // rest greyed — which is what stays on screen after raw mode is restored.
            "\x{1b}[1A\x{1b}[2K\x{1b}[1A\x{1b}[2K\x{1b}[1A\x{1b}[2K"
            "  \x{1b}[38;5;244mred\x{1b}[0m\r\n"
            "\x{1b}[38;5;77;1m> \x{1b}[0m\x{1b}[38;5;77;1mgreen\x{1b}[0m\r\n"
            "  \x{1b}[38;5;244mblue\x{1b}[0m\r\n");
}

// Go: TestSelectNumbered
TEST_CASE("select_numbered picks by index and repeats when out of range", "[ui][select]") {
    const std::vector<std::string> options{"red", "green", "blue"};

    SECTION("a valid choice returns the matching option") {
        std::istringstream in("2\n");
        const cup::ui::ScopedInput scoped(in);

        const auto got = cup::ui::detail::select_numbered("pick?", options);
        REQUIRE(got.has_value());
        REQUIRE(*got == "green");
    }

    SECTION("out of range, then valid") {
        std::istringstream in("9\n1\n");
        const cup::ui::ScopedInput scoped(in);

        const auto got = cup::ui::detail::select_numbered("pick?", options);
        REQUIRE(got.has_value());
        REQUIRE(*got == "red");
    }
}

// No Go counterpart: Go's selectNumbered took strconv.Atoi's error as the whole
// rejection rule, while parse_choice spells out four — from_chars failing, trailing
// bytes it did not consume, and either end of the range. Each is a way for a typed
// answer to be neither a choice nor an abort, and getting one wrong either indexes
// the option list out of bounds or refuses a legitimate answer.
TEST_CASE("select_numbered puts the question again for an answer it cannot use",
          "[ui][select]") {
    const std::vector<std::string> options{"red", "green", "blue"};

    // Every line but the last is unusable in a different way; the prompt loops on
    // each and only "2" ends it.
    std::istringstream in(
        "abc\n"  // not a number at all
        "1x\n"   // a number with something after it
        "0\n"    // below the first option: the list is 1-based
        "4\n"    // past the last option
        "2\n");
    const cup::ui::ScopedInput scoped(in);

    const auto got = cup::ui::detail::select_numbered("pick?", options);
    REQUIRE(got.has_value());
    REQUIRE(*got == "green");
}

// Go: TestSelectNumberedAbort
TEST_CASE("select_numbered aborts on EOF", "[ui][select]") {
    const std::vector<std::string> options{"a", "b"};
    std::istringstream in("");
    const cup::ui::ScopedInput scoped(in);

    const auto got = cup::ui::detail::select_numbered("pick?", options);
    REQUIRE_FALSE(got.has_value());
    REQUIRE(cup::error::is_abort(got.error()));
}

// Go: TestSelectNoOptions
TEST_CASE("select_one rejects an empty option list", "[ui][select]") {
    const std::vector<std::string> none;
    const auto got = cup::ui::select_one("pick?", none, "");
    REQUIRE_FALSE(got.has_value());
    REQUIRE(got.error().message() == "no options to choose from");
}

// No Go counterpart: the Go suite reached selectNumbered directly but never Select
// itself, so nothing checked that a piped run takes the numbered path. That is the
// path CI and every scripted `cup new` actually take.
TEST_CASE("select_one falls back to the numbered list off a terminal", "[ui][select]") {
    const std::vector<std::string> options{"red", "green", "blue"};

    // A pipe on the descriptor, so the fallback is taken even when the suite is run
    // by hand from a terminal — where select_one would otherwise enter raw mode and
    // sit waiting for a keypress. It stays empty: the numbered prompt reads through
    // ScopedInput, so the descriptor only has to be something that is not a tty.
    const ScopedStdin not_a_terminal("");
    REQUIRE_FALSE(cup::platform::is_tty(cup::platform::kStdinFd));

    std::istringstream in("3\n");
    const cup::ui::ScopedInput scoped(in);

    const auto got = cup::ui::select_one("pick?", options, "green");
    REQUIRE(got.has_value());
    REQUIRE(*got == "blue");
}

// The other half of that branch, and the path a user actually takes: on a terminal
// select_one enters raw mode and runs the arrow-key menu. Nothing reached it before
// — the suite only ever drove select_interactive directly — so neither the terminal
// check nor the raw-mode guard was exercised through the exported entry point.
TEST_CASE("select_one drives the arrow-key menu on a terminal", "[ui][select][keys]") {
    const std::vector<std::string> options{"red", "green", "blue"};

    // One key, and deliberately one: a single byte cannot be split across two reads,
    // so there is no arrangement of it that leaves the menu waiting. The arrow
    // sequences are three bytes and are covered against a pipe above, where the
    // framing is under the test's control.
    ScopedTerminalStdin terminal("\r");
    REQUIRE(cup::platform::is_tty(cup::platform::kStdinFd));

    CapturedStdout capture;
    const auto got = cup::ui::select_one("pick?", options, "blue");
    const std::string painted = capture.str();

    REQUIRE(terminal.typed_in_raw_mode());
    // The default decides where the cursor starts, so committing at once returns it
    // — and that is what tells the two paths apart: the numbered fallback would have
    // taken its own "1" default and answered "red".
    REQUIRE(got.has_value());
    REQUIRE(*got == "blue");
    REQUIRE(painted.contains("(up/down, enter)"));
}

// No Go counterpart: Go compared errors with errors.Is against ui.ErrAbort, and
// Error's kind plus its equality are the port of that comparison. Every prompt's
// abort path rests on it.
TEST_CASE("errors compare by kind and message", "[error]") {
    REQUIRE(cup::error::abort_error() == cup::error::abort_error());
    REQUIRE(cup::error::abort_error().kind() == Error::Kind::Abort);
    REQUIRE(cup::error::abort_error().message() == "aborted");

    // The same message with the ordinary kind is not the sentinel — matching Go,
    // where errors.Is compares identity rather than text.
    REQUIRE(Error("aborted").kind() == Error::Kind::General);
    REQUIRE_FALSE(Error("aborted") == cup::error::abort_error());
    REQUIRE_FALSE(cup::error::is_abort(Error("aborted")));

    REQUIRE(Error("boom") == Error("boom"));
    REQUIRE_FALSE(Error("boom") == Error("bang"));
    // A default-constructed Error carries no message and is not an abort.
    REQUIRE(Error{}.message().empty());
    REQUIRE_FALSE(cup::error::is_abort(Error{}));
}

// Regression guard for the scaffolding fix: a library scaffolded by `cup add lib`
// must be importable. Before cmd.primaryPreamble existed, cup generated an
// aggregator with no global module fragment, and GCC 14 then produced a BMI that
// failed to load here rather than in the module itself.
TEST_CASE("a scaffolded library is importable", "[scaffold][modules]") {
    REQUIRE_NOTHROW(cup::probe::Probe());
}
